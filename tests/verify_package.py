#!/usr/bin/env python3
"""Independent, read-only baseline package checks using only the Python standard library."""

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path


EGA = (
    (0x00, 0x00, 0x00, 0xFF), (0x55, 0x55, 0xFF, 0xFF),
    (0x55, 0xFF, 0x55, 0xFF), (0x55, 0xFF, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55, 0xFF), (0xFF, 0x55, 0xFF, 0xFF),
    (0xFF, 0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0xFF, 0xFF),
)

# Baseline 53 terrain IDs × 4 sub-cell quadrants.  This is an independent test fixture copied
# from reverse-engineering evidence, rather than assuming a direct file offset in MAIN.EXE.
TERRAIN_LUT = bytes.fromhex(
    "000000000f0f0f0f090909090e0e0e0e11111111000000030000040000010000"
    "0200000008000800000700070000050506060000090608000609000708000905"
    "0007050903090909090409090909010909090902090202000109000104000904"
    "0003030909080908070907090909060605050909000a00000b00000000000d00"
    "0c00000009020800010900070800090400070309090602000609000104000905"
    "0003050909100909100909090909091009091009090e09090e0909090909090e"
    "09090e0900110000110000000000001100001100"
)


def read_png_rgba(path: Path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError(f"{path}: invalid PNG signature")
    pos = 8
    width = height = color_type = bit_depth = None
    idat = bytearray()
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        pos += length + 12
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            assert (bit_depth, color_type, compression, filtering, interlace) == (8, 6, 0, 0, 0), path
        elif kind == b"IDAT":
            idat.extend(payload)
        elif kind == b"IEND":
            break
    assert width and height
    packed = zlib.decompress(idat)
    stride = width * 4
    assert len(packed) == height * (stride + 1)
    rows = []
    prior = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = packed[offset]
        offset += 1
        current = bytearray(packed[offset:offset + stride])
        offset += stride
        for index in range(stride):
            left = current[index - 4] if index >= 4 else 0
            above = prior[index]
            upper_left = prior[index - 4] if index >= 4 else 0
            if filter_type == 1:
                current[index] = (current[index] + left) & 0xFF
            elif filter_type == 2:
                current[index] = (current[index] + above) & 0xFF
            elif filter_type == 3:
                current[index] = (current[index] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                p = left + above - upper_left
                pa, pb, pc = abs(p - left), abs(p - above), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else (above if pb <= pc else upper_left)
                current[index] = (current[index] + predictor) & 0xFF
            elif filter_type != 0:
                raise AssertionError(f"{path}: unsupported PNG filter {filter_type}")
        rows.append(bytes(current))
        prior = current
    return width, height, rows


def png_pixel(decoded, x, y):
    width, height, rows = decoded
    assert 0 <= x < width and 0 <= y < height
    start = x * 4
    return tuple(rows[y][start:start + 4])


def u16(data, offset):
    return data[offset] | (data[offset + 1] << 8)


def terrain_id(newgame, sector_x, sector_y, sub_x, sub_y):
    block = (sector_y // 3) * 24 + sector_x // 3
    descriptor = u16(newgame, 0x0004 + block * 5)
    descriptor_slot = (sector_y % 3) * 3 + sector_x % 3
    chunk = u16(newgame, 0x05A4 + descriptor * 18 + descriptor_slot * 2)
    return newgame[0x1552 + chunk * 49 + (sub_y // 2) * 7 + sub_x // 2]


def latitude_band(sector_y):
    quotient = sector_y // 3
    folded = 5 - quotient if quotient < 6 else quotient - 6
    return abs(folded) // 2 if folded < 4 else folded - 2


def texture_pixel(map_put, band, texture, pixel_x, pixel_y):
    offset = 0x01B0 + (band * 18 + texture) * 0x6C + pixel_y * 9 + (pixel_x // 8) * 3
    bit = 7 - pixel_x % 8
    p0, p2, p1 = map_put[offset:offset + 3]
    color = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2)
    return EGA[color]


def expected_map_pixel(newgame, map_put, sector_x, sector_y, sub_x, sub_y, pixel_x, pixel_y):
    terrain = terrain_id(newgame, sector_x, sector_y, sub_x, sub_y)
    texture = TERRAIN_LUT[terrain * 4 + (sub_y % 2) * 2 + sub_x % 2]
    return texture_pixel(map_put, latitude_band(sector_y), texture, pixel_x, pixel_y)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    metadata_path = args.package / "world_map.json"
    required = (args.source / "NEWGAME.DAT", args.source / "MAP.PUT", args.source / "MAIN.EXE",
                args.source / "COLOR.CIM", metadata_path)
    if not all(path.is_file() for path in required):
        print("SKIP: baseline source or generated package is unavailable")
        return 77

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    require(metadata["schema_version"] == 6, "unexpected package schema")
    require(metadata["image"]["width"] == 24192 and metadata["image"]["height"] == 6048,
            "unexpected world image dimensions")
    require(len(metadata["ports"]) == 70, "unexpected active-port count")
    require(metadata["treasure_model"]["candidate_count"] == 13459, "unexpected candidate count")

    marker_info = metadata["port_marker"]
    marker_path = args.package / marker_info["path"]
    marker = read_png_rgba(marker_path)
    require((marker[0], marker[1]) == (24, 12), "unexpected port marker dimensions")
    require(hashlib.sha256(marker_path.read_bytes()).hexdigest() == marker_info["sha256"], "port marker hash mismatch")
    color = (args.source / "COLOR.CIM").read_bytes()
    for y in range(12):
        for x in range(24):
            source_offset = 0x0120 + y * 9 + (x // 8) * 3
            bit = 7 - x % 8
            p0, p2, p1 = color[source_offset:source_offset + 3]
            index = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2)
            require(png_pixel(marker, x, y) == EGA[index], f"port marker mismatch at {x},{y}")

    for port in metadata["ports"]:
        position = port["position"]
        sx, sy, ux, uy = (position["sector_x"], position["sector_y"],
                            position["sub_x"], position["sub_y"])
        require(position["pixel_x"] == (sx * 14 + ux) * 24, f"port {port['id']} x mismatch")
        require(position["pixel_y"] == (sy * 14 + uy) * 12, f"port {port['id']} y mismatch")
        signed_latitude = 90 - sy * 5 if sy < 18 else -(sy * 5 - 90)
        longitude_remainder = (sx + 38) % 72
        require(port["coordinates"]["latitude"]["signed_degrees"] == signed_latitude,
                f"port {port['id']} latitude mismatch")
        require(port["coordinates"]["longitude"]["east_degrees"] == longitude_remainder * 5,
                f"port {port['id']} longitude mismatch")
        political = port["political_status"]
        raw_attributes = port["attributes"]["raw"]
        ownership_code = raw_attributes & 0x03
        require(political["ownership_code"] == ownership_code, f"port {port['id']} ownership-code mismatch")
        require(political["independent"] == (ownership_code == 0), f"port {port['id']} independence mismatch")
        require(political["court_available"] == bool(raw_attributes & 0x10), f"port {port['id']} court mismatch")
        require(political["guild_available"] == bool(raw_attributes & 0x04), f"port {port['id']} guild mismatch")

    newgame = (args.source / "NEWGAME.DAT").read_bytes()
    map_put = (args.source / "MAP.PUT").read_bytes()
    require(metadata["terrain_lut"]["entries"] == len(TERRAIN_LUT), "terrain LUT length mismatch")
    require(metadata["terrain_lut"]["sha256"] == hashlib.sha256(TERRAIN_LUT).hexdigest(),
            "terrain LUT hash mismatch")
    samples = ((0, 0, 0, 0, 0, 0), (32, 8, 3, 12, 7, 5), (71, 17, 13, 13, 23, 11),
               (0, 18, 1, 2, 12, 7), (45, 27, 8, 9, 19, 2), (71, 35, 13, 13, 23, 11))
    decoded_tiles = {}
    for sx, sy, ux, uy, px, py in samples:
        world_x = (sx * 14 + ux) * 24 + px
        world_y = (sy * 14 + uy) * 12 + py
        tile_key = (world_x // 512, world_y // 512)
        if tile_key not in decoded_tiles:
            decoded_tiles[tile_key] = read_png_rgba(args.package / "tiles" / "L0" / f"{tile_key[0]}_{tile_key[1]}.png")
        actual = png_pixel(decoded_tiles[tile_key], world_x % 512, world_y % 512)
        expected = expected_map_pixel(newgame, map_put, sx, sy, ux, uy, px, py)
        require(actual == expected, f"terrain pixel mismatch at sector={sx},{sy} sub={ux},{uy} pixel={px},{py}")

    print("independent package analysis passed: marker, 70 ports, coordinates, 6 terrain samples")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, KeyError, OSError, struct.error, zlib.error, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
