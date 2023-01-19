#!/usr/bin/env python3

from socket import socket
import ssl
import json
import struct
import sys

with open("conf.json", 'r') as file:
    conf = json.load(file)

auth = bytes.fromhex(conf['auth'])
cmd = int(sys.argv[1])

sslctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
sslctx.check_hostname = False
sslctx.verify_mode = ssl.CERT_NONE  # TODO: Verify

with socket() as rawsock:
    with sslctx.wrap_socket(rawsock) as sock:
        sock.connect((conf['host'], conf['port']))
        sock.send(auth)
        sock.send(struct.pack("<I", cmd))
        buf = sock.recv()
        levels = struct.unpack("<2i", buf)
        print("levels:", *levels)
