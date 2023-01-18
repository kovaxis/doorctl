#!/usr/bin/env python3

# Create a packed conf hexstring

import struct


def s(name):
    return input(f"{name}: ").encode("UTF-8")


def ip(name):
    return bytes(map(int, input(f"{name} ip: ").split(".")))


def i(name):
    return int(input(f"{name}: "))


def h(name):
    return bytes.fromhex(input(f"{name}: "))


print(struct.pack("< 32s 32s 4s 4s 4s i 32s 32s 2i 2i i i", s("ssid"), s("password"), ip("server"), ip("gateway"),
      ip("subnet mask"), i("port"), h("rng"), h("secret"), i("out 1"), i("out 2"), i("in 1"), i("in 2"), i("open time"), i("threshold")).hex())
