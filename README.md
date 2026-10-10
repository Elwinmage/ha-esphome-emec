# ha-esphome-emec

**ESPHome external component to monitor and control an EMEC WDPHRH pH / Redox
pool controller (dosing pumps) over RS-485, natively in Home Assistant — no
cloud, no gateway, no Pi.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![ESPHome](https://img.shields.io/badge/ESPHome-external%20component-blue.svg)](https://esphome.io/components/external_components.html)

The ESP32 sits on the RS-485 bus like the EMEC BT ETH gateway does (and can
coexist with it). It reads the live
pH / Redox values, the alarms and every setting, and exposes all the writable
parameters (setpoints, dosing percentages, working modes, alarms, flow, digital
inputs…) as standard ESPHome entities.

It is the ESPHome twin of the EMEC support in
[ha-usr-modbus-bridge](https://github.com/Elwinmage/ha-usr-modbus-bridge): same
reverse-engineered protocol, same entities, but running on the ESP instead of
going through a USR-TCP232 gateway and a Home Assistant integration.

> **Status.** The protocol layer is a port of the one that runs in production
> in `ha-usr-modbus-bridge` (v1.4.x). This component has been validated against
> a simulated controller (unit test + full ESPHome `host` build driven through
> the native API), **not yet on a real WDPHRH from an ESP32**. Start by
> checking the readings before you rely on the writes, and please report back.

---

## Table of contents

- [How it works](#how-it-works)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Use it in your ESPHome YAML](#use-it-in-your-esphome-yaml)
- [Configuration reference](#configuration-reference)
- [Entities](#entities)
- [Protocol notes](#protocol-notes)
- [Troubleshooting](#troubleshooting)
- [Testing without the controller](#testing-without-the-controller)
- [Credits](#credits)
- [License](#license)

---

## How it works

The WDPHRH speaks an **ASCII protocol of the ERMES family**, not Modbus. On
this bus **the ESP is the master**:

- it sends a **heartbeat every 500 ms** — without it the controller stops
  answering;
- every `update_interval` it runs one **read cycle** of the 11 read commands
  (`valuer`, `setpntr`, `allrmr`…) and publishes what changed;
- a setting change is a **read-modify-write**: the group is read fresh, one
  field is replaced, the whole group is written back, then read again. The
  entity only shows the new value once the controller has acknowledged it, so
  a rejected write never leaves a wrong value in Home Assistant.

Queries are always sent right after a heartbeat, and nothing is transmitted
while the controller is talking, so the 500 ms cadence is never disturbed.

Everything runs locally on the ESP; Home Assistant only sees standard ESPHome
entities over the native API.

---

## Hardware

- **Any ESP32** (classic, S2, S3, C3, P4…).
- An **RS-485 transceiver**. **Strongly recommended: an auto-direction module**
  such as one based on the **MAX13487E** — it has only `VCC / TXD / RXD / GND`
  (no DE/RE pin) and switches direction on its own.
  - Avoid a plain 5 V **MAX485** driven from 3.3 V logic: its RO output is 5 V
    (not safe for most ESP32 pins).
- Power the transceiver from **3.3 V** so its RO/RXD line is safe for the ESP.

### A dedicated bus

- The controller needs its **own RS-485 bus**: 38400 baud and a frame every
  500 ms cannot share a Modbus RTU bus.
- The EMEC **BT ETH module can stay connected** (Nimbus / MyEmec keeps
  working), exactly as it does next to a USR-TCP232 with
  `ha-usr-modbus-bridge`. The component never transmits while the bus is busy
  and retries a query whose reply was lost.

---

## Wiring

```
ESP32 TX  (e.g. GPIO17) ──→ module TXD (DI)
ESP32 RX  (e.g. GPIO16) ←── module RXD (RO)
3.3V                    ──→ module VCC
GND                     ──── module GND
module A (D+)           ──── controller pin 1  (A = RS-485 +)
module B (D-)           ──── controller pin 2  (B = RS-485 −)
```

Notes:
- If the controller never answers, your **A/B are most likely swapped** — just
  invert the two bus wires (harmless to try).
- With an auto-direction module you do **not** set `flow_control_pin` in the
  `uart:` block.
- On a long cable, terminate the bus with 120 Ω at each end.

---

## Use it in your ESPHome YAML

Pull the component straight from GitHub — nothing to copy manually:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/Elwinmage/ha-esphome-emec
      ref: main
    components: [emec]

uart:
  id: rs485
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 38400        # enforced: the component refuses any other setting
  rx_buffer_size: 512

emec:
  id: wdphrh
  uart_id: rs485

sensor:
  - platform: emec
    emec_id: wdphrh
    ph:
      name: "pH"
    redox:
      name: "Redox"

binary_sensor:
  - platform: emec
    emec_id: wdphrh
    alarm_ph_empty:
      name: "pH- reservoir empty"
    alarm_chlorine_empty:
      name: "Chlorine reservoir empty"
    alarm_no_flow:
      name: "No flow"

number:
  - platform: emec
    emec_id: wdphrh
    ph_high_setpoint:
      name: "pH high setpoint"
    redox_low_setpoint:
      name: "Redox low setpoint"
```

Every entity is optional: declare only the ones you want. A full file with all
of them is in [`example.yaml`](example.yaml).

Prefer a local copy instead of Git? Drop `components/emec/` next to your YAML
and use:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [emec]
```

---

## Configuration reference

### `emec:` (hub)

| Option | Default | Description |
|--------|---------|-------------|
| `uart_id` | — | The `uart:` bus id (must be 38400 8N1). |
| `address` | `1` | RS-485 slave address of the controller (1–99). |
| `update_interval` | `10s` | Period of the full read cycle. |
| `heartbeat_interval` | `500ms` | Keepalive period. Leave it unless you know better. |
| `startup_delay` | `2s` | Heartbeat-only period after boot, before the first query. |
| `prefix` | `"34"` | Protocol family prefix, 2 characters. |
| `tau` | `0` | Trailing field of the `paramw` write. It cannot be read back, so this value is sent with every *passcode / feeding delay / priority* change. |

Several controllers (each on its own UART) can be declared: give each `emec:`
block an `id` and reference it with `emec_id` in the platforms.

### Platforms

Each platform takes `emec_id` plus any of the keys listed in
[Entities](#entities); every key accepts the standard ESPHome options of its
entity type (`name`, `icon`, `filters`, `disabled_by_default`…).

The `sync_clock` button additionally requires `time_id` (any ESPHome `time:`
source).

---

## Entities

### `sensor`

| Key | Description |
|-----|-------------|
| `ph` | Live pH |
| `redox` | Live Redox (mV) |
| `service_hours` | Service counter (h) |
| `service_offset` | Service offset |
| `valuer_field3`, `valuer_field4` | Raw, not yet understood fields of the `valuer` reply |

### `binary_sensor`

| Key | Description |
|-----|-------------|
| `connected` | The controller answers the read cycle |
| `alarm_ph_empty` | pH- reservoir empty (alarm bit `a1`) |
| `alarm_chlorine_empty` | Chlorine reservoir empty (`a2`) |
| `alarm_no_flow` | No flow (`a3`) |
| `standby` | Standby (`a8`) |
| `alarm_4` … `alarm_7` | Unidentified alarm bits |
| `output_1` … `output_3` | Output states |
| `dosing_alarm_ph_enabled`, `dosing_alarm_redox_enabled` | Dosing alarm threshold > 0 |
| `probe_failure_ph_enabled`, `probe_failure_redox_enabled` | Probe failure threshold > 0 |

### `number` (all writable)

| Key | Range | Writes via |
|-----|-------|------------|
| `ph_high_setpoint`, `ph_low_setpoint` | 0.00 – 14.00, step 0.05 | `setpnw` |
| `ph_high_dose`, `ph_low_dose` | 0 – 100 % | `setpnw` |
| `ph_waiting_time` | 0 – 99 min | `setpnw` |
| `redox_low_setpoint`, `redox_high_setpoint` | 0 – 999 mV, step 5 | `setpnw` |
| `redox_low_dose`, `redox_high_dose` | 0 – 100 % | `setpnw` |
| `redox_waiting_time` | 0 – 99 min | `setpnw` |
| `max_strokes_ph`, `max_strokes_redox` | 1 – 180 P/m | `setskw` |
| `passcode` | 0 – 9999 | `paramw` |
| `feeding_delay` | 0 – 60 min | `paramw` |
| `dosing_alarm_ph_threshold`, `dosing_alarm_redox_threshold` | 0 (off) or ≥ 1 min | `alldow` |
| `probe_failure_ph_threshold`, `probe_failure_redox_threshold` | 0 (off) or 100 – 250 min | `allprw` |
| `flow_time` | 0 – 99 min | `flowsw` |

### `select` (all writable)

| Key | Options | Writes via |
|-----|---------|------------|
| `ph_working_mode`, `redox_working_mode` | `PROPORTIONAL` / `ON/OFF` | `setpnw` |
| `priority_mode` | `No priority` / `pH priority` / `Redox priority` | `paramw` |
| `flow_mode` | `Disable` / `Reverse` / `Direct` | `flowsw` |
| `probe_failure_ph_mode`, `probe_failure_redox_mode` | `DOSE` / `STOP` | `allprw` |
| `dosing_alarm_ph_mode`, `dosing_alarm_redox_mode` | `DOSE` / `STOP` | `alldow` |

### `switch`

| Key | Description | Writes via |
|-----|-------------|------------|
| `standby_input_nc` | Standby digital input is N.C. (off = N.O.) | `diginw` |
| `ph_level_input_nc` | pH level input is N.C. | `diginw` |
| `redox_level_input_nc` | Redox level input is N.C. | `diginw` |

### `button`

| Key | Description | Writes via |
|-----|-------------|------------|
| `reset_alarms` | Clears the latched alarms | `resalw` |
| `sync_clock` | Pushes the ESP time to the controller (needs `time_id`) | `clockw` |

---

## Protocol notes

- Bus: **38400 8N1**, ASCII frames.
- Heartbeat: `34tb00 #0#\r\n` every ~500 ms.
- Query: `3401<cmd>\r\r` — note the **double CR**.
- Read reply: `34gpd01&WD#f1#f2#…#<cmd>end\r`.
- Write: `3401<cmd><payload><cmd>end\r\r`, where the payload is the read fields
  concatenated **without `#`**, each zero-padded to its fixed width.
- Write ACK: `34gpd01&<cmd>okend\r`; a frame containing `no#` … `end` is a
  rejection.

```
→ 3401setpntr\r\r
← 34gpd01&WD#0770#0730#040#000#00#0#0630#0670#050#000#00#0#setpntrend\r

→ 3401setpnw0760073004000000006300670050000000setpnwend\r\r
← 34gpd01&setpnwokend\r
```

The field layouts of every command are documented at the top of
[`components/emec/emec_protocol.h`](components/emec/emec_protocol.h), which is
plain C++ with no ESPHome dependency.

---

## Troubleshooting

Set `logger: level: DEBUG` to get a bus line every 30 s:

```
[emec]: bus: rx 5120 bytes / 233 frames, tx 310 frames, 110 replies, 0 timeouts
```

| Symptom | Likely cause / fix |
|---------|--------------------|
| `controller not answering`, `rx 0 bytes` | Nothing received: RX pin not wired to the module RO/RXD, module not powered, or bus not connected. With an auto-direction module you normally at least see your own frames echoed. |
| `rx` climbs but `0 replies` | **A/B swapped** or wrong `address`. |
| Readings fine, `write … failed (rejected or no ACK)` | The controller refused the value (e.g. a probe-failure threshold between 1 and 99) or answered `#no#change`. Check the value is one the front panel accepts. |
| Occasional `timeouts` | Harmless: each query is retried, and entities keep their last value. |
| `write refused: … out of range or queue full` | The value does not fit the field, or more than 16 writes are pending. |

---

## Testing without the controller

The protocol core is unit-tested off-target against a simulated controller:

```bash
g++ -std=c++17 -Wall -Wextra -I components/emec tests/test_protocol.cpp -o /tmp/emec_test && /tmp/emec_test
```

[`tests/pump_simulator.py`](tests/pump_simulator.py) emulates a WDPHRH on a
pseudo-terminal, which lets you run the whole component in an ESPHome `host:`
build (`uart: port: /tmp/emec_pty`):

```bash
python3 tests/pump_simulator.py --link /tmp/emec_pty
```

---

## Credits

- Protocol reverse-engineered by sniffing the EMEC BT ETH gateway on the
  RS-485 bus and correlating every field with the Nimbus / MyEmec UI.
- Companion project for a USR-TCP232 gateway + Home Assistant integration:
  [ha-usr-modbus-bridge](https://github.com/Elwinmage/ha-usr-modbus-bridge).
- Same approach for Hayward / Silverline heat pumps:
  [ha-esphome-hayward](https://github.com/Elwinmage/ha-esphome-hayward).

---

## License

MIT — see [LICENSE](LICENSE).
