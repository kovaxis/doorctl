#!/usr/bin/env python3

# Create a packed conf hexstring

import struct
import sys
import json
import io


def s(len):
    def codec(enc, val):
        if val is None:
            return f"{len}s"
        if enc:
            return val.encode("UTF-8")
        else:
            return val.decode("UTF-8").rstrip('\0')
    return codec


def ip(enc, val):
    if val is None:
        return "4s"
    if enc:
        return bytes(map(int, val.split(".")))
    else:
        return ".".join(map(str, val))


def i(enc, val):
    if val is None:
        return "i"
    return val


def h(len):
    def codec(enc, val):
        if val is None:
            return f"{len}s"
        if enc:
            return bytes.fromhex(val)
        else:
            return val.hex()
    return codec


def b(enc, val):
    if val is None:
        return "B"
    return val


schema = {
    "ssid": s(32),
    "password": s(32),
    "localip": ip,
    "gateway": ip,
    "subnet": ip,
    "port": i,
    "auth": h(32),
    "out1": i,
    "out2": i,
    "in1": i,
    "in2": i,
    "rep1": i,
    "rep2": i,
    "out_neg": b,
    "in_neg": b,
    "rep_neg": b,
    "threshold": i,
}


def encode(human: dict) -> bytes:
    machine = bytearray()
    for key, codec in schema.items():
        fmt = f"<{codec(None, None)}"
        machine += struct.pack(fmt, codec(True, human[key]))
    return bytes(machine)


def decode(machine: bytes) -> dict:
    stream = io.BytesIO(machine)
    human = {}
    for key, codec in schema.items():
        fmt = f"<{codec(None, None)}"
        piece = stream.read(struct.calcsize(fmt))
        human[key] = codec(False, *struct.unpack(fmt, piece))
    return human


if __name__ == "__main__":
    if sys.argv[1] == 'encode':
        human = json.load(sys.stdin)
        print(encode(human).hex())
    elif sys.argv[1] == 'decode':
        machine = bytes.fromhex(sys.stdin.read())
        print(json.dumps(decode(machine)))
    else:
        print(f"unknown mode {sys.argv[1]}")
