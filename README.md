# Aerauliqa Quantum NEXT for ESPHome

External component that drives an [Aerauliqa Quantum NEXT](https://www.aerauliqa.com/) heat-recovery ventilator over infrared. It targets **ESPHome 2026.11**. Older releases are out of scope: from 2026.10 a remote listener is counted only when codegen calls `register_listener`.

The frame format is [PROTOCOL.md](PROTOCOL.md). The encoder and decoder are [`components/quantum_next/protocol.h`](components/quantum_next/protocol.h). Humidity control and free cooling are unit-side. The remote cannot send them, and this component does not invent commands for them.

## Install

```yaml
external_components:
  - source: github://slashrsm/aeraliqua_esphome
    components: [quantum_next]

remote_transmitter:
  id: ir_tx
  pin: GPIO4
  carrier_duty_percent: 50%

remote_receiver:
  id: ir_rx
  pin:
    number: GPIO14
    inverted: true
    mode:
      input: true
      pullup: true
  tolerance: 25%
  filter: 50us
  idle: 10ms

quantum_next:
  id: qn
  name: Quantum NEXT
  transmitter_id: ir_tx
  receiver_id: ir_rx
```

`name` prefixes every entity. The component transmits at 38 kHz, twice, with 50 ms between the two copies. Keep Wi-Fi power save off (`wifi: power_save_mode: none`) or the receiver drops edges.

[`example/hailege.yaml`](example/hailege.yaml) is a complete config for the Hailege ESP8285 board (TX GPIO4, RX GPIO14). It loads this checkout instead of GitHub:

```bash
cp example/secrets.yaml.example example/secrets.yaml
esphome run example/hailege.yaml
```

## Entities

| Entity | What it does |
|---|---|
| Quantum NEXT | Fan with five speeds. Off stores the last speed, direction, and mode, and transmits an off frame. The next on restores them. |
| Quantum NEXT Direction | Alternate, Extract, or Intake. Stored while the fan is off, sent on the next on. |
| Quantum NEXT Mode | Comfort or Efficiency. Same. |
| Quantum NEXT Boost | Command 1 on, command 2 off. Refused while the fan is off. Does not rewrite speed or direction. Turning the fan off clears the switch. |
| Quantum NEXT Filter reset | Command 7. Refused while the fan is off. |
| Quantum NEXT IR debug | Diagnostic switch, default off. Logs `TX` and `RX` lines. There is no `dump: raw`. |

A received frame updates those entities and does not transmit. For 800 ms after a transmit the receiver ignores its own LED. The same frame inside 300 ms is ignored. Filter-reset repeats are about 500 ms apart, so the first one is kept.

The toggle bit is remembered. Each transmit uses the current value and then flips it. A received frame sets the next transmit to the opposite of what was heard. Power, speed, direction, mode, and toggle are stored in preferences. Nothing is sent at boot.

The unit has no temperature setpoint, so the component is a fan plus these controls.

## Development

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I components/quantum_next \
  tests/test_protocol.cpp -o /tmp/test_protocol && /tmp/test_protocol
```

GitHub Actions runs that test and `esphome compile example/hailege.yaml` on ESPHome 2026.11.
