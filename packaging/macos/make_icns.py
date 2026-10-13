"""Writes packaging/macos/siggen.icns from the PNG icons in assets/, without third-party packages.

The rounded square of the drawing is inset as on the macOS icon grid (824 of 1024 pixels), so the icon matches the
size of the others in the Dock. Sizes up to 256 come from the matching asset, resampled; 512 and 1024 are enlarged
from assets/icon.png, the largest one. Every size is stored as PNG, which macOS reads in icons since 10.7. Rerun it
after changing assets/icon*.png.
"""

import pathlib
import struct
import zlib

Assets = pathlib.Path(__file__).resolve().parents[2] / "assets"
Sources = {16: "icon_16.png", 32: "icon_32.png", 64: "icon_64.png", 256: "icon.png"}
# The icon types of each size; the @2x types of a size share its image
Types = {
    16: [b"icp4"],
    32: [b"icp5", b"ic11"],
    64: [b"ic12"],
    128: [b"ic07"],
    256: [b"ic08", b"ic13"],
    512: [b"ic09", b"ic14"],
    1024: [b"ic10"],
}
# The content spans 824 of the 1024 pixels of the macOS icon grid
Grid = 824 / 1024


def png_decode(data):
    """Returns (width, height, pixels) of an 8-bit RGBA PNG, pixels as a bytearray of RGBA rows."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos = 8
    idat = b""
    width = height = 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if (depth, color, interlace) != (8, 6, 0):
                raise ValueError("only 8-bit non-interlaced RGBA PNGs are supported")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray(height * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1 : pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
            elif kind != 0:
                raise ValueError(f"unknown PNG filter {kind}")
        out[y * stride : (y + 1) * stride] = line
        prev = line
    return width, height, out


def png_encode(width, height, pixels):
    """Returns the bytes of an 8-bit RGBA PNG of the given raw rows."""
    stride = width * 4
    raw = b"".join(b"\x00" + bytes(pixels[y * stride : (y + 1) * stride]) for y in range(height))
    chunks = b""
    for kind, body in [
        (b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)),
        (b"IDAT", zlib.compress(raw, 9)),
        (b"IEND", b""),
    ]:
        chunks += struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    return b"\x89PNG\r\n\x1a\n" + chunks


def resample(src, sw, sh, dw, dh):
    """Resamples RGBA rows to (dw, dh): bilinear when enlarging, box-averaged when reducing."""
    out = bytearray(dw * dh * 4)
    if dw > sw or dh > sh:
        for y in range(dh):
            sy = min(max((y + 0.5) * sh / dh - 0.5, 0.0), sh - 1.0)
            y0 = int(sy)
            y1 = min(y0 + 1, sh - 1)
            wy = sy - y0
            for x in range(dw):
                sx = min(max((x + 0.5) * sw / dw - 0.5, 0.0), sw - 1.0)
                x0 = int(sx)
                x1 = min(x0 + 1, sw - 1)
                wx = sx - x0
                o = (y * dw + x) * 4
                for c in range(4):
                    top = src[(y0 * sw + x0) * 4 + c] * (1 - wx) + src[(y0 * sw + x1) * 4 + c] * wx
                    bot = src[(y1 * sw + x0) * 4 + c] * (1 - wx) + src[(y1 * sw + x1) * 4 + c] * wx
                    out[o + c] = round(top * (1 - wy) + bot * wy)
    else:
        for y in range(dh):
            y0, y1 = y * sh / dh, (y + 1) * sh / dh
            for x in range(dw):
                x0, x1 = x * sw / dw, (x + 1) * sw / dw
                total = 0.0
                acc = [0.0] * 4
                sy = int(y0)
                while sy < y1:
                    hy = min(sy + 1, y1) - max(sy, y0)
                    sx = int(x0)
                    while sx < x1:
                        hx = min(sx + 1, x1) - max(sx, x0)
                        w = hx * hy
                        base = (sy * sw + sx) * 4
                        for c in range(4):
                            acc[c] += src[base + c] * w
                        total += w
                        sx += 1
                    sy += 1
                o = (y * dw + x) * 4
                for c in range(4):
                    out[o + c] = round(acc[c] / total)
    return out


def icon(size, images):
    """Returns the PNG bytes of one icns image: the drawing inset on the macOS grid, on a transparent square."""
    content = round(size * Grid)
    source = min((s for s in images if s >= content), default=max(images))
    sw, sh, pixels = images[source]
    drawing = resample(pixels, sw, sh, content, content)
    out = bytearray(size * size * 4)
    offset = (size - content) // 2
    for y in range(content):
        row = (offset + y) * size + offset
        out[row * 4 : (row + content) * 4] = drawing[y * content * 4 : (y + 1) * content * 4]
    return png_encode(size, size, out)


def main():
    images = {}
    for size, name in Sources.items():
        images[size] = png_decode((Assets / name).read_bytes())
    entries = b""
    for size, kinds in Types.items():
        image = icon(size, images)
        for kind in kinds:
            entries += kind + struct.pack(">I", 8 + len(image)) + image
    output = pathlib.Path(__file__).with_name("siggen.icns")
    output.write_bytes(b"icns" + struct.pack(">I", 8 + len(entries)) + entries)
    print(f"{output}: {len(Types)} sizes")


if __name__ == "__main__":
    main()
