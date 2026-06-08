# Meshtastic SX1302 Operation Guide

This document explains how to run `meshbridge` in two environments:
- development machine (no concentrator attached)
- concentrator host (SX1302 hardware attached)

## 1. Upstream Compile Prerequisites

Install base build tools and Meshtastic native dependencies (Ubuntu/Debian):

```sh
sudo apt-get update && sudo apt-get install -y \
  build-essential pkg-config git python3 python3-pip python3-venv \
  libyaml-cpp-dev libjsoncpp-dev libi2c-dev libbluetooth-dev \
  libgpiod-dev libuv1-dev libsdl2-dev libbsd-dev libssl-dev \
  libusb-1.0-0-dev libulfius-dev liborcania-dev
```

Install PlatformIO (pick one):

```sh
pipx install platformio
```

or:

```sh
python3 -m pip install --user -U platformio
```

If your distro uses an externally managed system Python (PEP 668), run PlatformIO via
its own virtualenv interpreter:

```sh
~/.platformio/penv/bin/python -m platformio run -e native
```

## 2. Build

From repo root:

```sh
cd meshtastic
make -j4
```

Build native firmware runtime (used by `--native` / `--dev`):

```sh
cd firmware
pio run -e native
```

## 3. Runtime Modes

### A. Development machine (no concentrator)

Use development mode. It starts native runtime IPC and skips `meshbridge` hardware init.

```sh
cd meshtastic
./run_ipc_stack.sh --dev
```

Equivalent command:

```sh
./run_ipc_stack.sh --native --skip-daemon
```

Notes:
- This mode is for IPC and runtime validation only.
- `timeout` returning exit code `143` is expected if you intentionally stop after a fixed duration.

### B. Concentrator host (with SX1302)

Start full stack with real daemon:

```sh
cd meshtastic
DAEMON_ARGS='-c /absolute/path/to/global_conf.json' ./run_ipc_stack.sh --native
```

If needed, force fail-fast when runtime IPC is unavailable:

```sh
MESHTASTIC_IPC_REQUIRED=1 DAEMON_ARGS='-c /absolute/path/to/global_conf.json' ./run_ipc_stack.sh --native
```

## 4. Configuration

`meshbridge` needs a valid gateway config JSON. Typical sources in this repo:

- `global_conf.json` in this directory for the MY_919 bridge profile
- `packet_forwarder/global_conf.json.*` legacy examples, which do not match the Meshtastic MY_919 slot grid

Example:

```sh
cd meshtastic
DAEMON_ARGS='-c /home/zaihan/Projects/sx1302_hal/meshtastic/global_conf.json' ./run_ipc_stack.sh --native
```

## 5. Quick Health Checks

Check launcher help:

```sh
cd meshtastic
./run_ipc_stack.sh -h
```

Check native runtime starts and binds socket:

```sh
cd meshtastic
timeout 12s ./run_ipc_stack.sh --dev
```

Expected indicators:
- `[MTK_NATIVE_IPC] listening on ...`
- `[ipc] runtime socket ready`
- region/frequency logs (for MY_919 builds):
  - `Set radio: region=MY_919`
  - `newRegion->freqStart -> newRegion->freqEnd: 919... -> 924...`

## 6. Troubleshooting

- `Runtime socket not ready`:
  - rebuild native firmware (`pio run -e native`)
  - ensure launcher uses native runtime path (`NATIVE_BIN`)
- `failed to find any configuration file named global_conf.json`:
  - pass explicit `DAEMON_ARGS='-c /abs/path/to/config.json'`
- hardware/SPI errors on dev machine:
  - use `--dev` mode; do not run full daemon mode
