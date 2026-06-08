# Meshtastic SX1302 Operation Guide

This document explains how to run `meshtasticd` in two environments:
- development machine (no concentrator attached)
- concentrator host (SX1302 hardware attached)

## 1. Build

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

## 2. Runtime Modes

### A. Development machine (no concentrator)

Use development mode. It starts native runtime IPC and skips `meshtasticd` hardware init.

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

## 3. Configuration

`meshtasticd` needs a valid gateway config JSON. Typical sources in this repo:

- `packet_forwarder/global_conf.json.sx1250.AS923.USB`
- other `packet_forwarder/global_conf.json.*` variants

Example:

```sh
cd meshtastic
DAEMON_ARGS='-c /home/zaihan/Projects/sx1302_hal/packet_forwarder/global_conf.json.sx1250.AS923.USB' ./run_ipc_stack.sh --native
```

## 4. Quick Health Checks

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

## 5. Troubleshooting

- `Runtime socket not ready`:
  - rebuild native firmware (`pio run -e native`)
  - ensure launcher uses native runtime path (`NATIVE_BIN`)
- `failed to find any configuration file named global_conf.json`:
  - pass explicit `DAEMON_ARGS='-c /abs/path/to/config.json'`
- hardware/SPI errors on dev machine:
  - use `--dev` mode; do not run full daemon mode
