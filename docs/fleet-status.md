# SAVE.DAT player fleet-status reconstruction

The F4 Fleet Status modal is read-only and always uses the currently selected allocated `SAVE.DAT` slot.

## Data evidence

Ghidra analysis of the original `MAIN.EXE` and the recovered original ship/status routines establish that a
7,594-byte (`0x1DAA`) save slot is a direct SHINARIO memory dump. Player fleet data is in the first five ship
slots; a present ship is identified only by `supply[+6] & 0x40`.

| Slot-relative offset | Layout | Fields used |
|---|---:|---|
| `0x0920` | 41 × 20 bytes | hero/captain names (15-byte name field) |
| `0x0C54` | 21 × 17 bytes | player fleet record 0; held gold at `+0x09` (LE16) |
| `0x126E` | 5 × 24 bytes | player cargo: captain, five goods IDs/quantities, food, water, lumber, morale |
| `0x12E6` | 11 × 21 bytes | ship name, maximum crew, cargo capacity |
| `0x14A0` | 106 × 7 bytes | first five records are player hull/sails/speed/guns/crew/type-active flags |

The original cargo-use routine sums five cargo quantities, food divided by 10, water divided by 10, and lumber
with 16-bit wraparound. The modal uses that same representation for `Cargo: used / capacity`. Food and water
are displayed in their original UI units (stored value divided by 10); lumber is stored and displayed directly.
Food, Water, and Lumber always appear first in the per-ship stock table. Trade-goods appear below them only when
their stored quantity is nonzero; invalid ship type or goods IDs are surfaced as `Unknown…` rather than being
silently interpreted.
