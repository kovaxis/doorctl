#!/usr/bin/env python3

from socket import socket
import ssl
import json
import struct
import sys

import packconf


with open("conf.json", 'r') as file:
    conf = json.load(file)
auth = bytes.fromhex(conf['auth'])


class Esp32:

    def __init__(self):

        sslctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        sslctx.check_hostname = False
        sslctx.verify_mode = ssl.CERT_NONE  # TODO: Verify

        self.sock = sslctx.wrap_socket(socket())
        self.sock.connect((conf['localip'], conf['port']))
        self.sock.send(auth)

    def close(self):
        self.sock.close()

    def send(self, fmt, *args):
        raw = struct.pack("<"+fmt, *args)
        print(f"sending {len(raw)} bytes in format {fmt}")
        self.sock.send(raw)

    def recv(self, fmt, cnt):
        print(f"receiving {cnt} bytes to parse into {fmt}")
        buf = bytearray()
        while len(buf) < cnt:
            piece = self.sock.recv(cnt - len(buf))
            if piece:
                print(f"  received {len(piece)}-byte piece")
            buf += piece
        return struct.unpack("<"+fmt, buf)

    def opendoor(self, num: int, time: int) -> int:
        print(f"opening door {num+1} for {time}ms")

        self.send("I", 1)
        self.send("BH", num, time)

        succ, = self.recv("B", 1)

        print(f"success: {succ}")
        return succ

    def readstatus(self) -> list[int]:
        print("reading values")

        self.send("I", 2)

        cnt, = self.recv('I', 4)
        vals = [self.recv('I', 4)[0] for i in range(cnt)]

        print(f"{cnt} pins:")
        for i in range(cnt):
            print(f"  pin #{i+1}: {vals[i]}")

        return vals

    def setconf(self, mode: str) -> int:
        assert mode in ['raw', 'text']
        print(f"setting conf with format {mode}")

        if mode == 'raw':
            # Raw conf hexstring
            conf = bytes.fromhex(sys.stdin.read())
        elif mode == 'text':
            # Structured conf
            conf = packconf.encode(json.load(sys.stdin))

        # Set config
        self.send("I", 3)
        cnt, = self.recv("I", 4)

        if cnt == len(conf):
            self.send(f"{cnt}s", conf)
            succ, = self.recv("B", 1)

            print(f"success: {succ}")
            return succ
        else:
            raise Exception(
                f"expected {cnt} bytes but given bytestring is {len(conf)} bytes")

    def getconf(self, mode: str) -> str:
        assert mode in ['raw', 'text']
        print(f"getting conf in format {mode}")

        self.send("I", 4)

        cnt, = self.recv("I", 4)
        conf, = self.recv(f"{cnt}s", cnt)

        if mode == 'raw':
            return f"{conf.hex()}"
        elif mode == 'text':
            return json.dumps(packconf.decode(conf))


if __name__ == '__main__':
    esp32 = Esp32()

    args = sys.argv[1:]
    args.reverse()

    cmd = args.pop()
    if cmd == 'open':
        num = int(args.pop()) - 1
        time = int(args.pop())
        esp32.opendoor(num, time)
    elif cmd == 'read':
        esp32.readstatus()
    elif cmd == 'conf':
        getset = args.pop()
        mode = args.pop()

        assert getset in ['get', 'set']

        if getset == 'set':
            esp32.setconf(mode)
        elif getset == 'get':
            print(esp32.getconf(mode))
    else:
        print("unknown command")
