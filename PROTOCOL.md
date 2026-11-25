# Aerauliqa Quantum NEXT IR protocol

The remote sends the whole LCD state in one Manchester frame. A toggle bit flips on each new press and carries no setting. The encoder and decoder are [`components/quantum_next/protocol.h`](components/quantum_next/protocol.h). The host test is [`tests/test_protocol.cpp`](tests/test_protocol.cpp).

The speed control is one button. It cycles 1, 2, 3, 4, 5, then back to 1.

## Physical layer

The captures are demodulated timings. Replay at 38 kHz. There is no leader.

| Symbol | On air |
|---|---|
| Half-bit | 450 µs |
| Bit period | 900 µs |
| Logical 1 | mark, then space |
| Logical 0 | space, then mark |

A run of two half-bits at the same level is one 900 µs pulse. Classify a received pulse as short below about 700 µs and long above that. Pulses shorter than 250 µs are noise. A pulse of 2000 µs or more is the gap after the frame. ESPHome reports that gap as 10000 µs when `idle` is 10 ms. Skip it. Do not treat it as data.

A frame is 33 bits, about 30 ms. If the last bit is a 1, its final space is swallowed by that gap, and a raw capture is one half-bit short. Put the space back before decoding. A leading mark is sometimes clipped. If the train starts with a short space, put one mark half-bit in front and decode again.

## Frame

33 bits, MSB first:

```
1 start | byte0 | byte1 | 00 | command byte
```

## Nibble check

Each byte carries one 4-bit value in the high nibble. The low nibble is the check:

- Even number of 1-bits: repeat the nibble. `0x9` → `0x99`, `0x6` → `0x66`, `0xA` → `0xAA`, `0xC` → `0xCC`.
- Odd number of 1-bits: follow it with its complement. `0xE` → `0xE1`, `0x1` → `0x1E`, `0x2` → `0x2D`, `0x8` → `0x87`, `0x7` → `0x78`.

A byte that matches neither form is corrupt.

## State

Drop the check nibbles. Byte 0 and byte 1 are one logical byte.

```
bit:  7        6    5    4      3       2        1          0
      toggle   S2   S1   S0     intake  extract  comfort    unused
```

Bits are numbered from the MSB, which is transmitted first.

**Toggle** is bit 7. It is not a checksum. The same LCD state is sent with both values. Speed 2, alternate, comfort is `AA E1` with toggle 1 and `2D E1` with toggle 0. The firmware flips the bit after each transmit. One direction press after a long pause kept the previous value. Every other observed press flipped it.

**Speed** is `S2 S1 S0`, binary 1 through 5 while the unit is on.

**Direction** is the pair `(intake, extract)`:

| Direction | intake | extract | Flag nibble with comfort, bit 0 clear |
|---|---|---|---|
| Alternate | 1 | 1 | `1110` = 0xE |
| Extract only | 0 | 1 | `0110` = 0x6 |
| Intake only | 1 | 0 | `1010` = 0xA |
| Off | 0 | 0 | comfort bit only |

**Comfort / efficiency** is bit 1. Comfort is 1, efficiency is 0. The bit is present in extract and intake as well. Intake plus efficiency was received as nibble `1000`.

**Bit 0** stayed 0 in every frame. Leave it 0.

## Command nibble

The last byte is another checked nibble. It is not part of the LCD speed or direction.

| Value | Byte | Meaning |
|---|---|---|
| 0 | `00` | Ordinary state |
| 1 | `1E` | Boost on |
| 2 | `2D` | Boost off |
| 7 | `78` | Filter reset |

**Boost** is the hourglass button. A short press sets the nibble to 1 and leaves speed, direction, and comfort as they are on the LCD. Pressing it again sets the nibble to 2. It does not return to 0. The unit itself then runs speed 5, extract only, for 15 minutes, and returns to the previous speed and direction. Boost cannot be started while the remote is off. If the LCD is already Speed 5 Extract, the hourglass lights and the remote sends nothing, because those fields already match what Boost would send apart from this nibble.

The Boost-off frame often ends on a space. That space is swallowed by the idle gap, so the capture is 32 bits until the space is restored. The restored frame from Speed 3, Alternate, Comfort was `B4 E1 00 2D`. The matching on frame was `33 E1 00 1E`.

**Filter reset** is a hold of about 5 seconds. The frame keeps the current speed, direction, and comfort bit, and sets the nibble to 7. From Speed 4, Alternate, Comfort the remote sent `CC E1 00 78` five times, about half a second apart, with the same toggle. The next ordinary speed press cleared it: Speed 5, Alternate, Comfort was `55 E1 00 00`.

## On and off

On frames use speed 1 through 5 and a direction other than off.

Off clears the speed field and both direction bits. It keeps the comfort bit. The toggle still flips. The command nibble on an off frame is 0.

| Previous mode | Toggle | Bytes |
|---|---|---|
| Comfort | 0 | `00 2D 00 00` |
| Comfort | 1 | `87 2D 00 00` |
| Efficiency | 0 | `00 00 00 00` |
| Efficiency | 1 | `87 00 00 00` |

Turning back on sends the previous speed, direction, and mode again. After `87 2D 00 00`, the next on frame was speed 2, extract, comfort (`2D 66 00 00`).

## Not sent by the remote

Smart humidity and free cooling run on the ventilation unit. This remote has no button that transmits them.

Humidity raises the speed for about 10 minutes when relative humidity rises quickly. Free cooling is the unit running extract-only or intake-only when heat recovery is not wanted.

## Examples

Speed 1, alternate, comfort, toggle 1, no command:

```
toggle, speed     = 1, 001
intake, extract   = 1, 1
comfort, bit 0    = 1, 0
logical           = 1001 1110
byte0             = 0x99
byte1             = 0xE1
bytes             = 99 E1 00 00
```

Toggle 0 of the same state is `1E E1 00 00`.

Speed 2, extract, efficiency, toggle 1, filter reset:

```
nibble0 = 1010 = 0xA → byte 0xAA
nibble1 = 0100 = 0x4 → byte 0x4B
command = 7    = 0x7 → byte 0x78
bytes   = AA 4B 00 78
```

## Seen on the air

Toggle 0 and toggle 1 are the same LCD state. Ordinary frames below have command nibble 0.

| LCD state | Toggle 0 | Toggle 1 |
|---|---|---|
| Speed 1, alternate, comfort | `1E E1` | `99 E1` |
| Speed 2, alternate, comfort | `2D E1` | `AA E1` |
| Speed 3, alternate, comfort | `33 E1` | `B4 E1` |
| Speed 4, alternate, comfort | `4B E1` | `CC E1` |
| Speed 5, alternate, comfort | `55 E1` | `D2 E1` |
| Speed 1, extract, comfort | `1E 66` | `99 66` |
| Speed 2, extract, comfort | `2D 66` | `AA 66` |
| Speed 1, intake, comfort | `1E AA` | `99 AA` |
| Speed 2, intake, comfort | `2D AA` | `AA AA` |
| Speed 1, alternate, efficiency | `1E CC` | `99 CC` |
| Speed 1, intake, efficiency | `1E 87` | `99 87` |
| Speed 2, extract, efficiency | `2D 4B` | `AA 4B` |
| Off, comfort kept | `00 2D` | `87 2D` |
| Off, efficiency kept | `00 00` | `87 00` |
| Boost on, speed 3, alternate, comfort | `33 E1 00 1E` | toggle 0, seen live |
| Boost off, speed 3, alternate, comfort | `B4 E1 00 2D` | toggle 1, seen live |
| Filter reset, speed 4, alternate, comfort | `CC E1 00 78` | toggle 1, repeated |

Other speed, direction, and mode combinations use the same fields. The component synthesises them. It does not keep a table of raw captures.
