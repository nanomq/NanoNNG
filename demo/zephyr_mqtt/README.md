# NanoNNG MQTT Client Demo for Zephyr RTOS

Async state-machine MQTT client running on Zephyr RTOS (qemu_x86, QEMU emulation).

## Overview

This demo follows the same pattern as [`demo/mqtt_async`](../mqtt_async/mqtt_async.c):

- **AIO callback state machine**: `INIT → RECV → WAIT → SEND` driven by `client_cb()`
- **`nng_dialer_create` + `nng_dialer_start`** for MQTT connection with `NNG_OPT_MQTT_CONNMSG`
- **`nng_ctx_recv` / `nng_ctx_send`** for fully asynchronous I/O
- **Connect/disconnect callbacks** for connection lifecycle monitoring

The client subscribes to `/zephyr/msg/1` and `/zephyr/msg/2`, then echoes any received PUBLISH payload to `/zephyr/msg/transfer`.

## Quick Start

### qemu_x86 (QEMU with SLIRP user-mode networking)

QEMU user-mode networking (SLIRP) is built into QEMU — **no host-side setup
is needed** (no socat, no TAP, no bridge).

```bash
cd demo/zephyr_mqtt

# Clean build
rm -rf build
west build -b qemu_x86 .

# Run (60 s timeout — DNS + TCP connect to broker.emqx.io may take a few seconds)
timeout 60 west build -d $(pwd)/build -t run
```

To exit QEMU: press `Ctrl+A`, then `x`.

### Build all boards (one-liners)

```bash
cd demo/zephyr_mqtt && rm -rf build && west build -b qemu_x86 .
cd demo/zephyr_mqtt && rm -rf build && west build -b mps2/an385 .   # build-only
```

### Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `Invalid opcode` crash | `-march=atom` vs QEMU `qemu32` mismatch | Fixed in `nanonng_external.cmake` — uses `-march=i686` |
| `MQTT connect timeout` | No internet / DNS unreachable | Verify host can `ping 8.8.8.8`; use local broker |
| Build fails | SDK not installed | Install Zephyr SDK 1.0.1+; verify `west --version` |

## Broker

Default: `mqtt-tcp://broker.emqx.io:1883` (EMQX public MQTT broker).

**Requirements:** Host machine needs internet access — DNS resolves `broker.emqx.io`
via QEMU SLIRP DNS proxy (`10.0.2.3`).

To use a local broker (no internet required), edit `BROKER_URL` in `main.c`:

```c
#define BROKER_URL  "mqtt-tcp://10.0.2.2:1883"
```

(`10.0.2.2` is the host machine under QEMU User Networking.) Then start a local
MQTT broker on port 1883:

```bash
# Option A: mosquitto
mosquitto -p 1883

# Option B: emqx
docker run -d --name emqx -p 1883:1883 emqx/emqx:latest

# Option C: nanomq
nanomq start
```

## QEMU Networking

**Mode: SLIRP (user-mode NAT)** via `CONFIG_NET_QEMU_USER=y`. QEMU emulates
a full TCP/IP stack in userspace — no host TAP, bridge, or socat needed.
This is NOT SLIP (serial-line IP); do NOT set up a SLIP socket.

| Address     | Role                  |
|-------------|-----------------------|
| `10.0.2.2`  | Host (gateway)        |
| `10.0.2.3`  | DNS proxy (SLIRP)     |
| `10.0.2.15` | Zephyr guest IP       |

DNS flow: Zephyr → `10.0.2.3` (QEMU DNS proxy) → host's `/etc/resolv.conf` nameservers.
Fallback: `8.8.8.8` (bypasses QEMU DNS proxy, but may be slower).

## Expected Output

After a successful run, the QEMU console shows:

```
=== NanoNNG Zephyr MQTT Client (async) ===
Broker: mqtt-tcp://broker.emqx.io:1883
MQTT: socket opened
MQTT: work allocated
MQTT: creating dialer for mqtt-tcp://broker.emqx.io:1883
MQTT: dialer created
MQTT: starting dialer (async)
MQTT: dialer started for mqtt-tcp://broker.emqx.io:1883
MQTT: connected
MQTT: subscribing 2 topics
MQTT: event loop running...
```

If the broker is unreachable or DNS fails:

```
MQTT connect timeout: Connection timed out
```

(No crash — the demo exits cleanly with a diagnostic message.)

## Verification

Once the demo prints "MQTT: event loop running...", publish a message to trigger
the echo:

```bash
# Publish to trigger the echo
mqttx pub -h broker.emqx.io -t /zephyr/msg/2 -m "hello from host"

# Or with mosquitto_pub
mosquitto_pub -h broker.emqx.io -t /zephyr/msg/2 -m "hello from host"
```

The Zephyr console shows:

```
MQTT RECV: 'hello from host' FROM: '/zephyr/msg/2'
MQTT SEND: 'hello from host' TO:   '/zephyr/msg/transfer'
```

Verify the echoed message:

```bash
mqttx sub -h broker.emqx.io -t /zephyr/msg/transfer
# or
mosquitto_sub -h broker.emqx.io -t /zephyr/msg/transfer
```

## Board Support

| Board | Status |
|-------|--------|
| `qemu_x86` | ✅ Build & Run (SLIRP user networking) |
| `mps2/an385` | ⚠️ Builds (ARM Cortex-M3, 4MB RAM); runtime needs Ethernet TAP config |
| `qemu_cortex_m3` | ⚠️ Builds but **cannot run** (64KB RAM insufficient) |

Board-specific configs live in [`boards/`](boards/). To add a new board, create
`boards/<BOARD>.conf` with the board's network and DNS settings.

### ARM Cortex-M Notes

On 32-bit ARM targets (Cortex-M3), NanoNNG uses a pthread-mutex fallback for
64-bit atomics (`NNG_ZEPHYR_NO_STDATOMIC`). `CONFIG_MAX_PTHREAD_MUTEX_COUNT`
should be set to at least 128 to accommodate the per-atomic mutex allocation.

## Configuration

See [`prj.conf`](prj.conf) for Zephyr Kconfig settings — POSIX threads, TCP
networking, DNS resolver, and memory pools.

| Setting | Value | Notes |
|---------|-------|-------|
| `CONFIG_HEAP_MEM_POOL_SIZE` | 262144 (256 KB) | MQTT handshake + payload buffers |
| `CONFIG_MAX_PTHREAD_MUTEX_COUNT` | 128 | Needed for 32-bit ARM atomics fallback |
| `CONFIG_MAX_PTHREAD_COND_COUNT` | 64 | Async I/O pipeline |
| `CONFIG_MAX_PTHREAD_RWLOCK_COUNT` | 32 | Internal locking |

The client waits up to 10 seconds for the MQTT CONNACK after dialing;
if the broker is unreachable or DNS fails, it reports a timeout instead
of crashing.

### Same Client-ID Reconnect

The demo sets `clean_session=true`.  If you restart the demo while the
broker still holds the previous session (e.g. unclean disconnect), the
broker will replace the old session — the new connection proceeds
normally.  The 256 KB heap ensures enough headroom for the concurrent
teardown + handshake path.
