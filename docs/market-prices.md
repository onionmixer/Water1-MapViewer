# SAVE.DAT port market-price reconstruction

The viewer reads market prices only from the selected, read-only original `SAVE.DAT` slot. It does not write a
save file or infer prices for data that the save does not contain.

## Evidence and record boundary

The Ghidra analysis of the original `MAIN.EXE`, corroborated by the reconstructed original market display, uses
these direct SHINARIO-memory offsets. A `SAVE.DAT` slot is a 7,594-byte (`0x1DAA`) direct SHINARIO dump.

| Slot-relative offset | Layout | Use |
|---|---:|---|
| `0x0DBA` | 50 × 18 bytes | port economic detail: population (`+0`), prosperity (`+0x0B`), market value (`+0x0C`, LE16) |
| `0x113E` | 50 × 5 bytes | port facility: region (`+0`), local specialty goods ID (`+1`), production threshold (`+2`) |
| `0x1786` | 70 × 20 bytes | map-port records |

The difference is intentional: only IDs `0..49` have an original market record. IDs `50..69` are valid map
ports, but their original `SAVE.DAT` slot contains no economy entry. The UI displays a clear unavailable-data
message for them.

## Formula implemented

The static table and goods names come from the verified baseline `MAIN.EXE` market table (original DS `0x2528`)
and goods-name pointer table (DS `0x20CA`). Dynamic inputs come from the selected slot.

Let `base = prosperity + 50`. Each price uses original integer division. The original **팔때** path clamps to
`1..9999`; the **살때** display path preserves its calculated value.

- **살때** (player buys from the port): use the first matching mature regional availability entry,
  `base × regional_factor / 100`; otherwise, if the local specialty is mature, use
  `base × market_value / 100`; otherwise show `—`.
- **팔때** (player sells to the port): use `base × regional_sell_factor / 100`. A mature local specialty
  overrides it with `(base × market_value / 100) / 2 + 1`.
- A regional/local entry is mature when `entry_threshold × 50 <= population`. The original display checks
  the regional availability list before the local specialty for **살때**.

The formulas and columns were checked against the original market-display control flow rather than treating the
`SAVE.DAT` bytes as a generic price list. The viewer lays the 26 rows into three horizontal `Item / Buy / Sell`
sets (nine table rows) without table scrollbars. Automated tests include a controlled slot fixture that verifies
regional and local-specialty prices, the 50-record boundary, the layout, and modal-close behavior.
