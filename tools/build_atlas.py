#!/usr/bin/env python3
"""Build a single-page RGBA PNG texture atlas and JSON metadata using only Python's stdlib."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _paeth(a: int, b: int, c: int) -> int:
    estimate = a + b - c
    da = abs(estimate - a)
    db = abs(estimate - b)
    dc = abs(estimate - c)
    if da <= db and da <= dc:
        return a
    return b if db <= dc else c


def read_png(path: Path) -> tuple[int, int, bytearray]:
    """Read non-interlaced, 8-bit RGB or RGBA PNG files."""
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path} is not a PNG file")

    offset = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
    compressed = bytearray()
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        kind = data[offset : offset + 4]
        offset += 4
        payload = data[offset : offset + length]
        offset += length + 4  # Skip the CRC, which zlib validates for IDAT data.
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if compression != 0 or filtering != 0:
                raise ValueError(f"{path} uses an unsupported PNG compression/filter method")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break

    if width is None or height is None or bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError(f"{path} must be non-interlaced 8-bit RGB or RGBA PNG")

    source_bpp = 4 if color_type == 6 else 3
    row_bytes = width * source_bpp
    decoded = zlib.decompress(compressed)
    expected = height * (row_bytes + 1)
    if len(decoded) != expected:
        raise ValueError(f"{path} has a truncated or malformed image stream")

    rows: list[bytearray] = []
    cursor = 0
    previous = bytearray(row_bytes)
    for _ in range(height):
        filter_type = decoded[cursor]
        cursor += 1
        filtered = decoded[cursor : cursor + row_bytes]
        cursor += row_bytes
        row = bytearray(row_bytes)
        for index, sample in enumerate(filtered):
            left = row[index - source_bpp] if index >= source_bpp else 0
            above = previous[index]
            upper_left = previous[index - source_bpp] if index >= source_bpp else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = _paeth(left, above, upper_left)
            else:
                raise ValueError(f"{path} uses an invalid PNG row filter")
            row[index] = (sample + predictor) & 0xFF
        rows.append(row)
        previous = row

    if color_type == 6:
        return width, height, bytearray().join(rows)

    rgba = bytearray(width * height * 4)
    destination = 0
    for row in rows:
        for index in range(0, len(row), 3):
            rgba[destination : destination + 3] = row[index : index + 3]
            rgba[destination + 3] = 255
            destination += 4
    return width, height, rgba


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def write_png(path: Path, width: int, height: int, rgba: bytes | bytearray) -> None:
    """Write an unfiltered, non-interlaced RGBA8 PNG."""
    row_bytes = width * 4
    scanlines = bytearray(height * (row_bytes + 1))
    for row in range(height):
        target = row * (row_bytes + 1)
        source = row * row_bytes
        scanlines[target] = 0
        scanlines[target + 1 : target + 1 + row_bytes] = rgba[source : source + row_bytes]
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(PNG_SIGNATURE + _png_chunk(b"IHDR", header) +
                     _png_chunk(b"IDAT", zlib.compress(scanlines, 9)) +
                     _png_chunk(b"IEND", b""))


def build_atlas(inputs: list[Path], padding: int, maximum_size: int) -> tuple[int, int, bytearray, dict]:
    images = []
    names = set()
    for path in inputs:
        width, height, pixels = read_png(path)
        name = path.stem
        if name in names:
            raise ValueError(f"duplicate atlas region name: {name}")
        names.add(name)
        if width + padding * 2 > maximum_size or height + padding * 2 > maximum_size:
            raise ValueError(f"{path} does not fit within the configured maximum size")
        images.append((name, width, height, pixels))
    images.sort(key=lambda image: (-image[2], -image[1], image[0]))

    placements = []
    cursor_x = padding
    cursor_y = padding
    row_height = 0
    used_width = used_height = 1
    for name, width, height, _ in images:
        if cursor_x + width + padding > maximum_size:
            cursor_x = padding
            cursor_y += row_height + padding
            row_height = 0
        if cursor_y + height + padding > maximum_size:
            raise ValueError("images do not fit in a single atlas page")
        placements.append((name, width, height, cursor_x, cursor_y))
        used_width = max(used_width, cursor_x + width + padding)
        used_height = max(used_height, cursor_y + height + padding)
        cursor_x += width + padding
        row_height = max(row_height, height)

    pixels = bytearray(used_width * used_height * 4)
    regions = {}
    image_by_name = {image[0]: image for image in images}
    for name, width, height, x, y in placements:
        source = image_by_name[name][3]
        for destination_y in range(y - padding, y + height + padding):
            source_y = min(max(destination_y - y, 0), height - 1)
            for destination_x in range(x - padding, x + width + padding):
                source_x = min(max(destination_x - x, 0), width - 1)
                source_index = (source_y * width + source_x) * 4
                destination_index = (destination_y * used_width + destination_x) * 4
                pixels[destination_index : destination_index + 4] = source[source_index : source_index + 4]
        regions[name] = {
            "x": x,
            "y": y,
            "width": width,
            "height": height,
            "uv": [x / used_width, (y + height) / used_height,
                   (x + width) / used_width, y / used_height],
        }
    return used_width, used_height, pixels, regions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path, help="input RGB/RGBA PNG sprite images")
    parser.add_argument("--output", type=Path, required=True, help="output atlas PNG")
    parser.add_argument("--metadata", type=Path, required=True, help="output region metadata JSON")
    parser.add_argument("--padding", type=int, default=2, help="extruded border pixels (default: 2)")
    parser.add_argument("--max-size", type=int, default=2048, help="maximum page side (default: 2048)")
    arguments = parser.parse_args()
    if arguments.padding < 0 or arguments.padding > 16 or arguments.max_size < 16:
        parser.error("padding must be 0..16 and max-size must be at least 16")

    try:
        width, height, pixels, regions = build_atlas(
            arguments.images, arguments.padding, arguments.max_size
        )
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.metadata.parent.mkdir(parents=True, exist_ok=True)
        write_png(arguments.output, width, height, pixels)
        arguments.metadata.write_text(
            json.dumps({"width": width, "height": height, "regions": regions}, indent=2) + "\n",
            encoding="utf-8",
        )
    except (OSError, ValueError, zlib.error) as error:
        print(f"atlas build failed: {error}", file=sys.stderr)
        return 1
    print(f"wrote {width}x{height} atlas with {len(regions)} regions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
