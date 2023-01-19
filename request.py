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
args = sys.argv[1:]
args.reverse()

sslctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
sslctx.check_hostname = False
sslctx.verify_mode = ssl.CERT_NONE  # TODO: Verify

with socket() as rawsock:
    with sslctx.wrap_socket(rawsock) as sock:
        sock.connect((conf['localip'], conf['port']))
        sock.send(auth)

        def send(fmt, *args):
            sock.send(struct.pack("<"+fmt, *args))

        def recv(fmt, cnt):
            print(f"receiving {cnt} bytes to parse into {fmt}")
            buf = bytearray()
            while len(buf) < cnt:
                piece = sock.recv(cnt - len(buf))
                if piece:
                    print(f"  received {len(piece)}-byte piece")
                buf += piece
            return struct.unpack("<"+fmt, buf)

        cmd = args.pop()
        if cmd == 'open':
            num = int(args.pop()) - 1
            time = int(args.pop())
            print(f"opening door {num+1} for {time}ms")

            send("I", 1)
            send("BH", num, time)

            succ, = recv("B", 1)

            print(f"success: {succ}")
        elif cmd == 'read':
            print("reading values")

            send("I", 2)

            cnt, = recv('I', 4)
            vals = [recv('I', 4)[0] for i in range(cnt)]

            print(f"{cnt} pins:")
            for i in range(cnt):
                print(f"  pin #{i+1}: {vals[i]}")
        elif cmd == 'conf':
            getset = args.pop()
            mode = args.pop()

            assert getset in ['get', 'set']
            assert mode in ['raw', 'text']

            if getset == 'set':
                print(f"setting conf with format {mode}")

                if mode == 'raw':
                    # Raw conf hexstring
                    conf = bytes.fromhex(sys.stdin.read())
                elif mode == 'text':
                    # Structured conf
                    conf = packconf.encode(json.load(sys.stdin))

                # Set config
                send("I", 3)
                cnt, = recv("I", 4)

                if cnt == len(conf):
                    send(f"{cnt}s", conf)
                    succ, = recv("B", 1)

                    print(f"success: {succ}")
                else:
                    print(
                        f"expected {cnt} bytes but given bytestring is {len(conf)} bytes")
            elif getset == 'get':
                print(f"getting conf in format {mode}")

                send("I", 4)

                cnt, = recv("I", 4)
                conf, = recv(f"{cnt}s", cnt)

                if mode == 'raw':
                    print(f"{conf.hex()}")
                elif mode == 'text':
                    print(json.dumps(packconf.decode(conf)))

        else:
            print("unknown command")
