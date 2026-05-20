# CMSIS-DAP Debug Probe

RP2350 Monitor firmware includes a CMSIS-DAP v2 SWD probe path based on the
Raspberry Pi official `debugprobe` project:

- Source baseline: `third_party/debugprobe`
- Low-level SWD engine: Raspberry Pi debugprobe PIO command model
- USB transport: CMSIS-DAP v2 bulk interface
- Control transport: existing USB CDC / Wi-Fi TCP JSONL `probe_*` commands

This keeps the monitor self-discovery firmware as the base image while adding
single-chip programming and debugging through normal host tools such as
OpenOCD/GDB. Flash algorithms, target memory maps, and target reset sequences
remain host-side OpenOCD responsibilities.

## USB Interfaces

The firmware enumerates as a composite USB device:

- CDC ACM: RP2350 Monitor JSONL control stream.
- Raspberry Pi reset vendor interface: keeps `picotool reboot -f -u` support.
- CMSIS-DAP v2 bulk vendor interface: used by OpenOCD and other CMSIS-DAP
  clients.

The CDC and CMSIS-DAP interfaces are independent. Do not open multiple host
programs against the same CDC JSONL serial stream at the same time.

## Pin Defaults

The default probe pinout matches Raspberry Pi debugprobe-on-Pico wiring:

| Signal | Default GPIO | Direction |
| --- | ---: | --- |
| SWCLK | GP2 | Pico output |
| SWDIO | GP3 | bidirectional |
| nRESET | GP1 | open-drain style, optional |
| GND | GND | common reference |

Pins are claimed through `PinManager`. If GP1/GP2/GP3 are already used by UART,
SPI, I2C, GPIO, or the logic analyzer, `probe_config` or the CMSIS-DAP connect
path reports a pin ownership failure instead of silently sharing the pins.

Use `probe_config` to move SWD to any exposed free GPIO pair:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_config --swclk 10 --swdio 11 --reset 12 --swclk-khz 1000
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_status
```

Use `--reset -1` when the target reset line is not connected.

## OpenOCD Flow

For default pins, OpenOCD can connect directly to the CMSIS-DAP USB bulk
interface:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg
```

For non-default pins, configure the monitor first through CDC or Wi-Fi, then
start OpenOCD:

```sh
python3 tools/rpmon_cli.py --serial /dev/tty.usbmodemXXXX probe_config --swclk 10 --swdio 11 --reset 12 --swclk-khz 1000
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg
```

OpenOCD target configuration must match the target MCU, not the Pico 2 W that
runs RP2350 Monitor. Examples:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg
openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg
```

Once OpenOCD is running, use GDB in the standard way:

```sh
arm-none-eabi-gdb firmware.elf
(gdb) target extended-remote localhost:3333
(gdb) monitor reset halt
(gdb) load
(gdb) continue
```

## AI-Controlled JSONL Commands

AI tooling can use the monitor control channel without scraping OpenOCD text.
The command flow is:

```json
{"cmd":"probe_caps"}
{"cmd":"probe_config","swclk":2,"swdio":3,"reset":1,"swclk_khz":1000}
{"cmd":"probe_status"}
{"cmd":"probe_reset","action":"pulse","pulse_ms":50}
```

`probe_dap` exposes a raw CMSIS-DAP packet bridge for scripts that want direct
DAP command access through USB CDC or Wi-Fi TCP:

```json
{"cmd":"probe_dap","packet_hex":"00f0"}
```

The response includes `resp_hex`. For the packet above, byte 0 is
`ID_DAP_Info` and byte 1 is `DAP_ID_CAPABILITIES`; the returned bytes are the
normal CMSIS-DAP response packet.

## Resource Rules

- Only SWD is enabled. JTAG and SWO report unsupported.
- The probe uses one PIO state machine from PIO0 or PIO1 and the selected GPIOs.
- Logic analyzer still uses PIO2/DMA and can run concurrently only when GPIOs
  do not overlap with probe pins.
- UART/SPI/I2C/GPIO channels and probe pins are mutually exclusive by ownership.
- OpenOCD direct debug uses USB CMSIS-DAP. Wi-Fi can carry the JSONL
  `probe_dap` bridge, but it is not a low-latency GDB transport.

## Recovery

The USB reset vendor interface is intentionally preserved. After flashing this
firmware, `picotool reboot -f -u` should still switch the Pico 2 W into BOOTSEL
mode for AI-driven firmware updates.
