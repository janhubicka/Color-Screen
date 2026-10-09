#!/usr/bin/env python3
"""Generate a tiny, deterministic, uncompressed Bayer DNG for RAW cache tests.

Only Python's standard library is required. The fixture is intentionally
created in the build tree: no copyrighted camera RAW or large binary test
asset is checked into the source distribution.
"""

import math
import struct
import sys

T_BYTE, T_ASCII, T_SHORT, T_LONG, T_RATIONAL, T_SRATIONAL = 1, 2, 3, 4, 5, 10
WIDTH, HEIGHT = 64, 48


def encoded(kind, values):
    if kind == T_BYTE:
        return bytes(values)
    if kind == T_ASCII:
        return values.encode("ascii") + b"\0"
    if kind == T_SHORT:
        return struct.pack("<" + "H" * len(values), *values)
    if kind == T_LONG:
        return struct.pack("<" + "I" * len(values), *values)
    if kind in (T_RATIONAL, T_SRATIONAL):
        return b"".join(struct.pack("<" + ("II" if kind == T_RATIONAL else "ii"),
                                    x, y) for x, y in values)
    raise ValueError(kind)


def write_fixture(path):
    # TIFF 6.0 IFD plus DNG v1.4 CFA metadata. We provide a simple D65
    # identity camera matrix and a 2x2 RGGB pattern, not color-corrected pixels.
    tags = [
        (254, T_LONG, [0]),           # NewSubfileType: main image
        (256, T_LONG, [WIDTH]),
        (257, T_LONG, [HEIGHT]),
        (258, T_SHORT, [16]),
        (259, T_SHORT, [1]),          # Uncompressed
        (262, T_SHORT, [32803]),      # CFA
        (273, T_LONG, [0]),           # StripOffsets, patched below
        (274, T_SHORT, [1]),
        (277, T_SHORT, [1]),
        (278, T_LONG, [HEIGHT]),
        (279, T_LONG, [WIDTH * HEIGHT * 2]),
        (284, T_SHORT, [1]),
        (33421, T_SHORT, [2, 2]),     # CFARepeatPatternDim
        (33422, T_BYTE, [0, 1, 1, 2]),  # CFAPattern: RGGB
        (50706, T_BYTE, [1, 4, 0, 0]), # DNGVersion
        (50707, T_BYTE, [1, 1, 0, 0]), # BackwardVersion
        (50708, T_ASCII, "ColorScreen Synthetic Bayer"),
        (50710, T_BYTE, [0, 1, 2]),  # CFAPlaneColor
        (50711, T_SHORT, [1]),       # CFALayout rectangular
        (50713, T_SHORT, [1, 1]),
        (50714, T_SHORT, [64]),
        (50717, T_LONG, [65535]),
        (50718, T_RATIONAL, [(1, 1), (1, 1)]),
        (50721, T_SRATIONAL,
         [(1, 1), (0, 1), (0, 1), (0, 1), (1, 1), (0, 1),
          (0, 1), (0, 1), (1, 1)]),
        (50728, T_RATIONAL, [(1, 1), (1, 1), (1, 1)]),
        (50778, T_SHORT, [21]),      # D65 CalibrationIlluminant1
    ]
    tags.sort(key=lambda item: item[0])
    n = len(tags)
    first_extra_offset = 8 + 2 + n * 12 + 4
    extra = bytearray()
    entries = []
    for tag, kind, values in tags:
        data = encoded(kind, values)
        count = len(data) // {T_BYTE: 1, T_ASCII: 1, T_SHORT: 2,
                              T_LONG: 4, T_RATIONAL: 8, T_SRATIONAL: 8}[kind]
        if len(data) <= 4:
            encoded_field = data.ljust(4, b"\0")
        else:
            # TIFF offsets are at least word aligned.
            if len(extra) % 2:
                extra.append(0)
            offset = first_extra_offset + len(extra)
            encoded_field = struct.pack("<I", offset)
            extra.extend(data)
        entries.append((tag, kind, count, encoded_field))
    if len(extra) % 2:
        extra.append(0)
    strip_offset = first_extra_offset + len(extra)
    ifd = bytearray(struct.pack("<H", n))
    for tag, kind, count, field in entries:
        if tag == 273:
            field = struct.pack("<I", strip_offset)
        ifd.extend(struct.pack("<HHI", tag, kind, count))
        ifd.extend(field)
    ifd.extend(struct.pack("<I", 0))

    mosaic = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            # A spatial gradient with colour-specific structure so that the
            # interpolators produce distinct, nonconstant RGB images.
            signal = 9000 + 270 * x + 175 * y + int(
                4200 * math.sin(0.22 * x + 0.13 * y))
            which = ((y & 1) << 1) | (x & 1)
            gain = (1.10, 0.86, 0.92, 0.72)[which]
            sample = max(64, min(65535, int(signal * gain)))
            mosaic.extend(struct.pack("<H", sample))

    with open(path, "wb") as out:
        out.write(b"II" + struct.pack("<HI", 42, 8))
        out.write(ifd)
        out.write(extra)
        out.write(mosaic)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: generate-bayer-dng.py OUTPUT.dng")
    write_fixture(sys.argv[1])
