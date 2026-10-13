"""Packs packaging/windows/siggen.ico from the PNG icons in assets/, without third-party packages.

Windows reads PNG entries in .ico files since Vista, so the images are embedded as they are: assets/ stays the
single source of the icon, and this script only writes the container. Rerun it after changing assets/icon*.png.
"""

import pathlib
import struct

Assets = pathlib.Path(__file__).resolve().parents[2] / "assets"
Sources = ["icon_16.png", "icon_32.png", "icon_48.png", "icon_64.png", "icon.png"]


def png_size(data):
    """Returns the (width, height) of a PNG, read from its IHDR chunk."""
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError("not a PNG file")
    width, height = struct.unpack(">II", data[16:24])
    if width != height:
        raise ValueError("icon PNGs must be square")
    return width


def main():
    images = []
    for name in Sources:
        data = (Assets / name).read_bytes()
        images.append((png_size(data), data))
    offset = 6 + 16 * len(images)
    directory = struct.pack("<HHH", 0, 1, len(images))
    for size, data in images:
        # The size byte is the dimension modulo 256: zero means 256, the only size that does not fit a byte
        directory += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    output = pathlib.Path(__file__).with_name("siggen.ico")
    output.write_bytes(directory + b"".join(data for _, data in images))
    sizes = ", ".join(str(size) for size, _ in images)
    print(f"{output}: {len(images)} sizes ({sizes})")


if __name__ == "__main__":
    main()
