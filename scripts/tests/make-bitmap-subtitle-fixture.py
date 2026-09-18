"""Deterministic PGS test data, decoded by the product's FFmpeg (no custom decoder).

Two authored rectangles, partial alpha, clear display sets and palette changes.
Output paths are explicit; invoke FFmpeg separately to mux with a test video.
"""
import pathlib
import struct
import sys


def segment(seconds, kind, payload):
    return b"PG" + struct.pack(">IIBH", round(seconds * 90000), 0, kind, len(payload)) + payload


def display(seconds, sequence, visible, palette=0):
    objects = [(0, 100, 600), (1, 900, 100)] if visible else []
    pcs = struct.pack(">HHBHBBBB", 1280, 720, 0x10, sequence, 0x80, 0, palette, len(objects))
    for object_id, x, y in objects:
        pcs += struct.pack(">HBBHH", object_id, 0, 0, x, y)
    data = segment(seconds, 0x16, pcs)
    if visible:
        data += segment(seconds, 0x17, struct.pack(">BBHHHH", 1, 0, 0, 0, 1280, 720))
        # YCrCb + alpha: white and half-transparent red, swapped on the second display.
        entries = [(235, 128, 128, 255), (81, 240, 90, 128)]
        if palette:
            entries.reverse()
        pds = bytes([palette, 0, 0, 16, 128, 128, 0])
        for index, color in enumerate(entries, 1):
            pds += bytes([index, *color])
        data += segment(seconds, 0x14, pds)
        for object_id, _, _ in objects:
            # Alternating colored halves with a transparent border.
            row = bytes([0, 0x84, 0, 0, 0x9C, 1, 0, 0x9C, 2, 0, 0x84, 0, 0, 0])
            pixels = row * 24
            ods = struct.pack(">HBB", object_id, 0, 0xC0)
            ods += (len(pixels) + 4).to_bytes(3, "big") + struct.pack(">HH", 64, 24) + pixels
            data += segment(seconds, 0x15, ods)
    return data + segment(seconds, 0x80, b"")


target = pathlib.Path(sys.argv[1])
target.parent.mkdir(parents=True, exist_ok=True)
target.write_bytes(display(1, 0, True) + display(3, 1, False) + display(5, 2, True, 1) + display(7, 3, False))
print(target)
