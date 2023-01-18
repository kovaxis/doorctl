#!/usr/bin/env python3

from socket import socket
from hashlib import sha256
import json

with open("conf.json", 'r') as file:
    conf = json.load(file)

auth = bytes.fromhex(conf['auth'])
cmd = int(input("cmd: "))

PARAM_CODE = 250
RESULT_CODE = 251

print("connecting")
s = socket()
s.connect((conf['host'], conf['port']))
print("connected")

challenge = s.recv(32)
print(f"received challenge '{challenge.hex()}'")

solved = sha256()
solved.update(challenge)
solved.update(auth)
s.send(solved.digest())
print(f"sent solved challenge '{solved.digest().hex()}'")

cmdhash = sha256()
cmdhash.update(bytes([PARAM_CODE]))
cmdhash.update(bytes([cmd]))
cmdhash.update(challenge)
cmdhash.update(auth)
s.send(cmdhash.digest())
print(f"sent command '{cmdhash.digest().hex()}'")

reshash = s.recv(32)
print(f"received response '{reshash.hex()}'")

res = None
for maybe_res in range(256):
    mres = sha256()
    mres.update(bytes([RESULT_CODE]))
    mres.update(bytes([maybe_res]))
    mres.update(challenge)
    mres.update(auth)
    if mres.digest() == reshash:
        res = maybe_res

print(f"response: {res}")
