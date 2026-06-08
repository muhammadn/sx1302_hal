# Meshtastic Bridge Architecture

This bridge mirrors the ClusterDuck SX1302 bridge pattern and provides a thread-safe handoff between `meshbridge` (C) and a Meshtastic runtime.

## Files

- `MeshtasticBridge.h`: C/C++ public API
- `MeshtasticBridge.cpp`: thread-safe queue/callback implementation

## C API

- `mtk_bridge_register_rx_callback(...)`
- `mtk_bridge_handle_uplink(...)`
- `mtk_bridge_enqueue_downlink(...)`
- `mtk_bridge_enqueue_downlink_ext(...)`
- `mtk_bridge_pop_downlink(...)`
- `mtk_bridge_get_stats(...)`
- `mtk_bridge_reset_stats(...)`

## Notes

- Uplink path supports callback mode and queue-based polling mode.
- Downlink path is queue-based with optional radio metadata.
- `MeshtasticSX126xShim` provides SX126x-oriented integration entrypoints.
- Runtime code should provide strong implementations for:
  - `meshtastic_sx126x_runtime_init()`
  - `meshtastic_sx126x_runtime_loop()`
  - `meshtastic_sx126x_runtime_send(...)`

If not provided, weak fallbacks keep `meshbridge` buildable for bridge-level testing.

## Stability-first runtime integration

To keep Meshtastic core upgrade-safe, runtime integration is provided as a standalone
adapter layer (`MeshtasticRuntimeAdapter.cpp`) behind build flags:

- `ENABLE_MESHTASTIC_RUNTIME_ADAPTER=1`
  - Compiles strong `meshtastic_sx126x_runtime_*` hooks.
  - Wires outbound Meshtastic TX to `mtk_bridge_enqueue_downlink_ext(...)`.
  - Pumps inbound bridge uplinks into Meshtastic via a `RadioInterface` adapter.
  - Requires Meshtastic Linux runtime headers/toolchain (Portduino-style environment)
    passed via `MESHTASTIC_RUNTIME_INCLUDES=...`.

- `MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS=0`
  - Makes missing strong runtime hooks fail fast (production-friendly).
  - Default is `1` for development bring-up.

## Linux daemon builds

For this repository, the supported default is Linux daemon build without direct
Meshtastic core adapter compilation:

```sh
make -C meshtastic -j4 MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS=0
```

This is hardware-agnostic and does not require ESP32 headers. If you later enable
`ENABLE_MESHTASTIC_RUNTIME_ADAPTER=1`, provide Linux Meshtastic runtime include
paths using `MESHTASTIC_RUNTIME_INCLUDES`, because Meshtastic core types depend on
Arduino-compatible platform headers.

## Implemented: Linux-first IPC runtime path

The default build now enables a process-boundary runtime bridge for Linux daemons.
`meshbridge` links `MeshtasticRuntimeIpc.c`, which provides strong
`meshtastic_sx126x_runtime_*` symbols and communicates with an external Meshtastic
runtime process over a Unix domain `SOCK_SEQPACKET` socket.

### Build flags

- `ENABLE_MESHTASTIC_RUNTIME_IPC=1` (default)
  - Enables external runtime IPC bridge (recommended architecture).
- `ENABLE_MESHTASTIC_RUNTIME_ADAPTER=1`
  - Enables in-process adapter build (requires Meshtastic SDK/toolchain includes).
  - Cannot be enabled together with IPC mode.

### Runtime environment variables

- `MESHTASTIC_IPC_SOCKET`
  - Unix socket path used by `meshbridge`.
  - Default: `/tmp/meshtastic-sx1302.sock`.
- `MESHTASTIC_IPC_REQUIRED`
  - `1` to fail startup if IPC runtime is unavailable.
  - Default: `0` (degraded mode allowed).

### Message directions

- Uplink (`sx1302 -> runtime`): concentrator packet + RF metadata.
- Downlink (`runtime -> sx1302`): payload + TX scheduling/modulation metadata.
- App send (`meshtastic_send_data -> runtime`): control/user payload from daemon.

This keeps Meshtastic core outside the SX1302 daemon process while preserving a
strict runtime ABI boundary.

### One-command launcher

Use the helper script from `meshtastic/` to start runtime + daemon with matching
`MESHTASTIC_IPC_SOCKET` wiring:

```sh
cd meshtastic
./run_ipc_stack.sh --stub
```

Run only the runtime side (IPC smoke check):

```sh
cd meshtastic
./run_ipc_stack.sh --stub --skip-daemon
```

Use real native runtime instead of stub:

```sh
cd meshtastic
./run_ipc_stack.sh --native
```

Development machine mode (non-concentrator safe):

```sh
cd meshtastic
./run_ipc_stack.sh --dev
```

`--dev` is equivalent to `--native --skip-daemon` and is intended for hosts
without SX1302 concentrator hardware.

Useful overrides:

```sh
MESHTASTIC_IPC_SOCKET=/tmp/custom.sock MESHTASTIC_IPC_REQUIRED=1 ./run_ipc_stack.sh --native
```

## IPC Runtime Stub (test-only)

`MeshtasticRuntimeStub.c` is a bring-up/testing helper, not a production runtime.
It lets you validate socket connectivity, framing, reconnect behavior, and downlink
path integration before a full Linux Meshtastic runtime is ready.

Build:

```sh
make -C meshtastic runtime-stub
```

Run (log-only mode):

```sh
./meshtastic/meshtastic_runtime_stub
```

Run with simple downlink echo for integration tests:

```sh
MTK_STUB_ECHO_DOWNLINK=1 ./meshtastic/meshtastic_runtime_stub
```

Change socket path if needed:

```sh
MESHTASTIC_IPC_SOCKET=/tmp/custom.sock ./meshtastic/meshtastic_runtime_stub
```

### Production policy

- Do not deploy `meshtastic_runtime_stub` in production.
- Production requires a real Meshtastic Linux runtime implementing the same IPC contract.
- Set `MESHTASTIC_IPC_REQUIRED=1` in production so `meshbridge` fails fast if runtime IPC is unavailable.

### Production-readiness checklist

- Real runtime process replaces stub service.
- Runtime implements all required routing/crypto/channel logic.
- IPC reconnection and backpressure behavior verified under restart/load tests.
- Soak test passed (24h minimum, 72h preferred).
- Monitoring and alerts configured for reconnects, queue depth, and downlink misses.

## Deferred Plan: Multi-profile (multi-SF) support

This section defines a later-phase implementation plan for supporting multiple
LoRa modulation profiles (for example SF7/SF9/SF12) with SX1302 + Meshtastic.

### 1) Profile model and configuration

Implementation:
- Add a profile table in bridge configuration with a stable `profile_id`.
- Each profile must include frequency, SF, bandwidth, coding rate, and optional
  sync or preamble values.
- Add a config schema version to prevent silent incompatibilities.

Acceptance criteria:
- Service refuses startup when any required profile field is missing.
- Config validation error clearly identifies invalid profile and field.

### 2) IPC / bridge contract extension

Implementation:
- Extend uplink/downlink message schema to include:
  - `profile_id`
  - full RF metadata (freq, sf, bw, cr, rssi, snr, timestamps)
- Add protocol `version` field and reject unknown major versions.

Acceptance criteria:
- Packets without `profile_id` are rejected with metric increment.
- Version mismatch is logged once per interval (rate-limited) and does not crash.

### 3) Runtime isolation strategy

Implementation:
- Choose one:
  - one Meshtastic context per profile in one process, or
  - one Meshtastic process per profile (recommended for stronger isolation)
- Keep shared supervision and IPC ownership in Linux daemon layer.

Acceptance criteria:
- Restarting one profile context does not stop packet flow for other profiles.
- Health endpoint reports each profile independently.

### 4) Deterministic routing policy

Implementation:
- Uplink: route by explicit `profile_id`; do not infer from payload.
- Downlink: enforce destination profile match before scheduling.
- Unknown profile behavior: drop + metric + structured warning log.

Acceptance criteria:
- No cross-profile packet leakage during mixed-profile load tests.
- Routing decisions are reproducible and traceable via logs.

### 5) Scheduler and compliance guards

Implementation:
- Validate per-profile TX timing windows before enqueue.
- Enforce duty-cycle and collision checks using existing JIT queue controls.
- Add profile-aware prioritization policy (control traffic over bulk payloads).

Acceptance criteria:
- Missed-deadline downlinks are counted and surfaced by profile.
- No scheduler crash on malformed or late packets.

### 6) Observability and SLOs

Implementation:
- Add per-profile metrics:
  - uplink packets, downlink attempts, TX success/failure
  - queue depth and drop counters
  - end-to-end latency (ingress to scheduled TX)
- Add heartbeat and reconnect counters for runtime bridge channel.

Acceptance criteria:
- Metrics can identify the worst profile within 5 minutes of incident start.
- Alert thresholds configurable per profile.

### 7) Reliability validation

Implementation:
- Add automated tests for:
  - mixed SF traffic
  - process restart and reconnect
  - malformed metadata and protocol version mismatch
- Run soak tests (24h minimum, 72h preferred).

Acceptance criteria:
- No uncontrolled process exits during soak tests.
- Packet loss and latency stay within defined SLO targets.

### 8) Rollout gates

Implementation:
- Phase rollout:
  1. single profile baseline
  2. two profiles canary
  3. full profile set
- Promote only after meeting acceptance criteria at each gate.

Acceptance criteria:
- Rollback can be performed by config toggle only (no redeploy required).
- Gate report archived for each promotion step.

### Suggested initial SLO targets (tune later)

- Uplink processing failure rate: < 0.1%
- Downlink scheduling miss rate: < 0.5%
- Bridge reconnect MTTR: < 10 seconds
- P99 ingress-to-schedule latency: < 250 ms
