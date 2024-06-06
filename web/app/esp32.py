#!/usr/bin/env python3

import asyncio
import socket
import json
import struct
import sys
import logging
import time

from blake3 import blake3

from app import packconf

log = logging.getLogger("esp32")


with open("./conf-esp32.json", 'r') as file:
    conf = json.load(file)
auth = bytes.fromhex(conf['auth'])


class Esp32:
    sock: socket.socket
    timeline: int
    clock_base: int
    min_timeout: float
    max_timeout: float
    timeout: float

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect((conf["localip"], conf['port']))
        self.retries = 5
        self.min_timeout = 0.2
        self.max_timeout = 2
        self.timeout = 0.2
        self.timeline = 0
        self.clock_base = 0

    async def close(self):
        self.sock.close()

    async def message(self, payload: bytes) -> bytes | None:
        # Prepare message to send
        send_time = self.clock_base + round(time.monotonic() * 1000_000)
        raw = struct.pack("<QQ", self.timeline, send_time) + payload
        raw = blake3(raw + auth).digest() + raw

        # Send/receive
        give_up_on = time.monotonic() + self.max_timeout
        while time.monotonic() <= give_up_on:
            # Send message
            self.sock.send(raw)
            log.debug("sent %s bytes with timeline %s and timestamp %s", len(raw), self.timeline, send_time)

            # Receive message
            buf = None
            while True:
                try:
                    self.sock.settimeout(min(self.timeout, max(0.001, give_up_on - time.monotonic())))
                    buf = await asyncio.get_event_loop().sock_recv(self.sock, 1024)
                except TimeoutError:
                    self.timeout = min(self.timeout * 2, self.max_timeout)
                    log.debug("raised timeout to %ss", self.timeout)
                    break

                # Decode reply
                signature = buf[:32]
                buf = buf[32:]
                signature_payload = buf
                timeline, expiration, now, status = struct.unpack("<QQQB", buf[:8+8+8+1])
                buf = buf[8+8+8+1:]

                if blake3(signature_payload + auth).digest() != signature:
                    log.warn("received a message with an invalid signature")
                    continue

                if expiration != send_time:
                    log.debug("skipping message with expiration id %s (expected %s)", expiration, send_time)
                    continue

                break

            if buf is None:
                continue

            self.timeout = max(self.timeout - 0.1, self.min_timeout)
            break
        else:
            log.debug("gave up on request")
            raise TimeoutError()
        
        log.debug("received a reply with timeline %s, current timestamp %s, status %s and payload %s", timeline, now, status, buf)
        self.timeline = timeline
        self.clock_base = now - round(time.monotonic() * 1000_000) + 4000_000
        if status == 1:
            return buf
        elif status == 2:
            log.debug("missed esp32 response, but esp carried out the action: %s", buf.decode(errors="replace"))
            return None
        else:
            raise Exception("esp32 returned an error: " + buf.decode(errors="replace"))

    async def opendoor(self, num: int, time: int) -> str:
        log.debug("opening door %s for %sms", num+1, time)

        result = await self.message(struct.pack("<IBH", 1, num, time))
        if result is None:
            result = "door already opened"
        else:
            result = result.decode(errors="replace")

        log.debug("success: %s", result)
        return result

    async def readstatus(self) -> list[int] | None:
        log.debug("reading values")

        raw = await self.message(struct.pack("<I", 2))
        if raw is None:
            log.debug("missed the pin values response")
            return None
        vals = [x[0] for x in struct.iter_unpack("<I", raw)]
        log.debug("pin levels: %s", vals)
        vals = [v > 0 for v in vals]

        return vals

    async def setconf(self, mode: str) -> int:
        assert mode in ['raw', 'text']
        log.info("setting conf with format %s from stdin", mode)

        if mode == 'raw':
            # Raw conf hexstring
            conf = bytes.fromhex(sys.stdin.read())
        elif mode == 'text':
            # Structured conf
            conf = packconf.encode(json.load(sys.stdin))

        # Set config
        written, = struct.unpack("<I", await self.message(struct.pack("<I", 3) + conf))
        
        if written != len(conf):
            log.warn("wrote a %s-byte config but the esp32 expects a %s-byte config", len(conf), written)
        
        log.info("config written")

    async def getconf(self, mode: str) -> str:
        assert mode in ['raw', 'text']
        log.debug("getting conf in format %s", mode)

        conf = await self.message(struct.pack("<I", 4))

        if mode == 'raw':
            return f"{conf.hex()}"
        elif mode == 'text':
            return json.dumps(packconf.decode(conf))


if __name__ == '__main__':
    async def main():
        esp32 = Esp32()
        try:
            await esp32.readstatus()
        except Exception as e:
            # Just make sure timeline and clock are synchronized
            log.debug("sync call: %s", e)

        args = sys.argv[1:]
        args.reverse()

        cmd = args.pop()
        if cmd == 'open':
            num = int(args.pop()) - 1
            time = int(args.pop())
            await esp32.opendoor(num, time)
        elif cmd == 'read':
            await esp32.readstatus()
        elif cmd == 'conf':
            getset = args.pop()
            mode = args.pop()

            assert getset in ['get', 'set']

            if getset == 'set':
                await esp32.setconf(mode)
            elif getset == 'get':
                print(await esp32.getconf(mode))
        else:
            log.error("unknown command")
        
        await esp32.close()

    logging.basicConfig()
    log.setLevel(logging.DEBUG)
    asyncio.run(main())
