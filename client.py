#!/usr/bin/env python3

from PIL import Image
import sys
import socket
import struct
import random
import string
import hashlib

def solve_pow(prefix, difficulty):
    while True:
        nonce = ''.join(random.choices(string.ascii_letters + string.digits, k=16))
        nonce = nonce.encode('ascii')
        h = hashlib.sha256(prefix + nonce).digest()[::-1]
        h = h.hex()
        print(h)
        if int(h, 16) & ((1 << difficulty)-1) == 0:
            return nonce

def send_pixel(x, y, r, g, b):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        #server_address = ("172.29.165.125", 8080)
        server_address = ("127.0.0.1", 8080)
        # Pack x and y as 16-bit little-endian values
        message = struct.pack('<HH', x, y)
        print("connected")
        sock.sendto(message, server_address)
        # Receive the response

        resp = sock.recv(1024)
        difficulty = resp[7]
        nonce = solve_pow(resp, difficulty)

        message = resp + nonce + struct.pack('BBB', r, g, b)
        print(message)
        print(sock.sendto(message, server_address))


def send_image(path):
    img = Image.open(path).convert("RGB")
    pixels = img.load()

    for y in range(img.height):
        for x in range(img.width):
            r, g, b = pixels[x, y]
            send_pixel(x, y, r, g, b)

if __name__ == "__main__":
    send_image(sys.argv[1])
