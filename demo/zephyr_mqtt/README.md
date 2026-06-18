# NanoNNG MQTT Client Demo for Zephyr RTOS

Async state-machine MQTT client running on Zephyr RTOS (qemu_x86).

## Overview

This demo follows the same pattern as [`demo/mqtt_async`](../mqtt_async/mqtt_async.c):

- **AIO callback state machine**: `INIT → RECV → WAIT → SEND` driven by `client_cb()`
- **`nng_dialer_create` + `nng_dialer_start`** for MQTT connection with `NNG_OPT_MQTT_CONNMSG`
- **`nng_ctx_recv` / `nng_ctx_send`** for fully asynchronous I/O
- **Connect/disconnect callbacks** for connection lifecycle monitoring

The client subscribes to `/zephyr/msg/1` and `/zephyr/msg/2`, then echoes any received PUBLISH payload to `/zephyr/msg/transfer`.

## Quick Start

```bash
# Build
cd demo/zephyr_mqtt
west build -b qemu_x86 .

# Run
west build -t run
```

## Broker

Default: `mqtt-tcp://broker.emqx.io:1883` (EMQX public broker).

To use a local broker, edit `BROKER_URL` in `main.c`:

```c
#define BROKER_URL  "mqtt-tcp://10.0.2.2:1883"
```

(`10.0.2.2` is the host machine under QEMU User Networking.)

## QEMU Networking

Uses `CONFIG_NET_QEMU_USER=y` (SLIRP) — **no host-side TAP or bridge setup needed**.

| Address     | Role                  |
|-------------|-----------------------|
| `10.0.2.2`  | Host (gateway)        |
| `10.0.2.3`  | DNS proxy (SLIRP)     |
| `10.0.2.15` | Zephyr guest IP       |

DNS servers: `10.0.2.3` (QEMU SLIRP) and `8.8.8.8` (Google, fallback).

## Test with MQTTX

```bash
# Publish to trigger the echo
mqttx pub -h broker.emqx.io -t /zephyr/msg/2 -m "hello from host"

# Subscribe to see echoed messages
mqttx sub -h broker.emqx.io -t /zephyr/msg/transfer
```

Expected output on QEMU console:

```
MQTT RECV: 'hello from host' FROM: '/zephyr/msg/2'
MQTT SEND: 'hello from host' TO:   '/zephyr/msg/transfer'
```

## Configuration

See [`prj.conf`](prj.conf) for Zephyr Kconfig settings — POSIX threads, TCP networking, DNS resolver, and memory pools.
