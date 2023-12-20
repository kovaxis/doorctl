#!/usr/bin/env python3

from asyncio import StreamReader, StreamWriter
import asyncio
import ssl
import json
import struct
import sys
import logging

from app import packconf

log = logging.getLogger("esp32")
log.setLevel(logging.DEBUG)


with open("./conf-esp32.json", 'r') as file:
    conf = json.load(file)
auth = bytes.fromhex(conf['auth'])


class Esp32:
    reader: StreamReader
    writer: StreamWriter

    def __init__(self):
        pass

    async def connect(self):
        sslctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        sslctx.check_hostname = False
        sslctx.verify_mode = ssl.CERT_NONE  # TODO: Verify

        log.debug("doing ssl handshake with esp32")
        self.reader, self.writer = await asyncio.open_connection(conf['localip'], conf['port'], ssl=sslctx, ssl_handshake_timeout=8.0)
        log.debug("sending authentication string")
        self.writer.write(auth)
        await self.writer.drain()

    async def close(self):
        self.writer.close()
        await self.writer.wait_closed()

    async def send(self, fmt, *args):
        raw = struct.pack("<"+fmt, *args)
        log.debug("sending %s bytes in format %s", len(raw), fmt)
        self.writer.write(raw)
        await self.writer.drain()

    async def recv(self, fmt, cnt):
        log.debug("receiving %s bytes to parse into %s", cnt, fmt)
        buf = await self.reader.readexactly(cnt)
        return struct.unpack("<"+fmt, buf)

    async def opendoor(self, num: int, time: int) -> int:
        log.debug("opening door %s for %sms", num+1, time)

        await self.send("I", 1)
        await self.send("BH", num, time)

        succ, = await self.recv("B", 1)

        log.debug("success: %s", succ)
        return succ

    async def readstatus(self) -> list[int]:
        log.debug("reading values")

        await self.send("I", 2)

        cnt, = await self.recv('I', 4)
        vals = [(await self.recv('I', 4))[0] for i in range(cnt)]

        log.debug("%s pins:", cnt)
        for i in range(cnt):
            log.debug("  pin #%s: %s", i+1, vals[i])

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
        await self.send("I", 3)
        cnt, = await self.recv("I", 4)

        if cnt == len(conf):
            await self.send(f"{cnt}s", conf)
            succ, = await self.recv("B", 1)

            log.debug(f"success: {succ}")
            return succ
        else:
            raise Exception(
                f"expected {cnt} bytes but given bytestring is {len(conf)} bytes")

    async def getconf(self, mode: str) -> str:
        assert mode in ['raw', 'text']
        log.debug("getting conf in format %s", mode)

        await self.send("I", 4)

        cnt, = await self.recv("I", 4)
        conf, = await self.recv(f"{cnt}s", cnt)

        if mode == 'raw':
            return f"{conf.hex()}"
        elif mode == 'text':
            return json.dumps(packconf.decode(conf))


if __name__ == '__main__':
    async def main():
        esp32 = Esp32()
        await esp32.connect()

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
