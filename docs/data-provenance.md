# Data provenance

`Water1_MapViewer` reads original files supplied by the user and does not bundle those files.

## Supported baseline profile

| File | Size | SHA-256 | Use |
|---|---:|---|---|
| `NEWGAME.DAT` | 70,677 | `da0e78e42fff86935a137d0b9b25296a262b3e4fd8fef6c965ee181cea77510f` | Hierarchical terrain data |
| `MAP.PUT` | 9,072 | `7cc1be5a31a22941449a0c8cfa4ab02864452ed4cb923e5023f1336470ad638a` | Terrain textures |
| `SHINARIO.CIM` | 7,595 | `9bef680aa42239275f3a073072a5f3e8f3b712c253496946a646dd8c6291bf20` | Port records |
| `COLOR.CIM` | 2,568 | `ac8728961453e4f91a5c7f78aabed30205752d3cc7c1dbbe9453de228260c7ba` | Original 24×12 port marker sprite |
| `MAIN.EXE` | 338,277 | `0b6795578e8c968c05f04cac1b91d71249833400d843594a1faaa121819ff9a2` | Terrain quadrant LUT provenance |

## Terrain quadrant LUT

The 212-byte table in `src/WorldMapGenerator.cpp` maps `terrain_id (0..52) × quadrant (0..3)` to a
`MAP.PUT` texture ID (`0..17`). It is a source-controlled derived table for the baseline profile, reconstructed
from the original's logical `DS:0x2414` table by reverse engineering. `MAIN.EXE` is packed, so `0x4C0E4` is
not treated as a direct raw-file LUT offset. The program validates the baseline executable hash before enabling
package generation, so it does not silently apply this LUT to a different executable profile.

## EGA terrain palette and display aspect

`MAP.PUT` terrain textures contain three planes (mask order 1, 4, 2), which yield 3-bit colors `0..7`.
The original running map uses the bright EGA attribute-controller mapping held at `MAIN.EXE` data segment
offset `0x2C48`; `COLOR.CIM` is sprite data and is not a palette. The generator maps indices as follows:

```text
0 black       (0,   0,   0)
1 bright blue (85,  85, 255)
2 bright green(85, 255,  85)
3 bright cyan (85, 255, 255)
4 bright red  (255,85,  85)
5 bright magenta (255,85,255)
6 yellow      (255,255, 85)
7 white       (255,255,255)
```

Generated pixels use Qt's native-endian ARGB32 format so `QRgb` channel order is preserved on little-endian
systems. The viewer applies the original 640×200-in-a-4:3-frame pixel aspect, horizontal:vertical = 5:12.
The exported PNG retains raw 24×12 EGA cells for lossless data correspondence, and records the same 5:12
aspect in its PNG `pHYs` metadata (12,000×5,000 pixels per metre); its JSON repeats the display rule for
viewers that do not honor PNG physical-resolution metadata. A future profile must validate its palette against
original rendering evidence before being marked fully supported.

## Original port marker

`COLOR.CIM` is not used as a terrain palette. Its sprite metadata is nevertheless part of the supported
baseline profile: sprite id 3 starts at offset `0x0120`, has width 3 bytes (24 pixels) and height 12 pixels.
The bytes are decoded in each triplet as plane 1, plane 4, plane 2, using the same bright EGA mapping above;
color 0 is opaque black, matching the original plane-replace blitter. The generator exports this exact decoded
sprite as `port_marker.png`, hashes it in `world_map.json`, and the viewer places its top-left at the port cell
top-left, matching the original world-map draw call.

## Initial port political status

For the 70 active 20-byte port records at `SHINARIO.CIM` offset `0x1786`, the record byte `0x0E` is retained
as an unclassified raw field; it is not used as sovereignty. The original engine uses the low two bits of
`PORT_ATTR_B` (record byte `0x13`) for the initial political code: `0` independent/neutral, `1` Portugal,
`2` Spain, `3` Ottoman Turkey. Bit `0x10` means the Court ruler facility is available (King for Portugal/Spain,
Sultan for Ottoman Turkey); bit `0x04` is Guild availability and bit `0x08` is initial discovery. These derived
fields are stored in each package's `political_status` object and are independently checked against the raw byte.
