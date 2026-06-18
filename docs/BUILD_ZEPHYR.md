# Building NanoNNG for Zephyr RTOS

This guide covers setting up a Zephyr development environment, building NanoNNG as a Zephyr external project, and running the demo applications on QEMU x86.

## Prerequisites

Follow the official Zephyr [Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) to set up your development environment.

Minimum versions (from Zephyr v4.4):

| Tool              | Min. Version |
|-------------------|--------------|
| CMake             | 3.20.5       |
| Python            | 3.12         |
| Devicetree compiler | 1.4.6      |
| Zephyr SDK        | 1.0.0        |

The demos in this project target the `qemu_x86` board. Any Linux distribution supported by Zephyr (Ubuntu, Fedora, Debian, etc.) should work.

## Environment Setup

### 1. Docker Container (Recommended)

```bash
# Pull Zephyr official development image from GitHub Container Registry
docker pull ghcr.io/zephyrproject-rtos/zephyr-build:main

# Run container with host workspace mounted
docker run -d \
    --name zephyr-tap \
    --cap-add=NET_ADMIN \
    --device=/dev/net/tun \
    -p 2222:22 \
    -v ~/Projects/EMQ/ZephyrProject:/workdir \
    ghcr.io/zephyrproject-rtos/zephyr-build:main \
    sleep infinity

docker exec -it -u root zephyr-tap bash

source /opt/python/venv/bin/activate
```

The host workspace is mounted at `/workdir` inside the container. NanoNNG source is at `/workdir/NanoNNG`.

### 2. Manual Setup (Without Docker)

```bash
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/zephyr-sdk-1.0.1_linux-x86_64.tar.xz
tar xf zephyr-sdk-1.0.1_linux-x86_64.tar.xz -C /opt/toolchains/
cd /opt/toolchains/zephyr-sdk-1.0.1 && ./setup.sh

pip install west
west init ~/zephyrproject && cd ~/zephyrproject
west update
pip install -r zephyr/scripts/requirements.txt
```

## Project Structure

```
NanoNNG/
├── CMakeLists.txt              # Top-level: NNG_PLATFORM_ZEPHYR option
├── src/platform/zephyr/        # Zephyr platform abstraction layer
│   ├── zephyr_thread.c         #   pthread-based threading
│   ├── zephyr_atomic.c         #   64-bit atomics via mutex fallback
│   ├── zephyr_clock.c          #   Monotonic time & sleep
│   ├── zephyr_file.c           #   File I/O (CONFIG_FILE_SYSTEM guarded)
│   ├── zephyr_resolv_gai.c     #   Synchronous DNS resolver
│   ├── zephyr_sockfd.c         #   Socket operations
│   ├── zephyr_tcpdial.c        #   TCP dialer
│   ├── zephyr_tcplisten.c      #   TCP listener
│   ├── zephyr_tcpconn.c        #   TCP connection
│   ├── zephyr_udp.c            #   UDP operations
│   └── zephyr_pollq_poll.c     #   Poll-based event loop
├── zephyr/                     # Zephyr module manifest
└── demo/
    ├── zephyr/                 # inproc PAIRv0 demo
    └── zephyr_mqtt/            # MQTT async client demo
```

## Build & Run: inproc Demo

A minimal PAIRv0 test over inproc transport. No networking required.

```bash
cd demo/zephyr
west build -b qemu_x86 .
west build -t run
```

Expected output:
```
=== NanoNNG Zephyr Demo ===
PASS: pair_open  PASS: listen  PASS: dial
PASS: send       PASS: recv    PASS: close
ALL TESTS PASSED
```

### Build System Notes

NanoNNG is built via CMake `ExternalProject_Add` at ninja time:

```cmake
-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}    # Zephyr SDK cross-compiler
-DCMAKE_C_FLAGS=${external_cflags}         # Zephyr includes + defines
-DCMAKE_SYSTEM_NAME=Generic                # Cross-compilation mode
-DNNG_PLATFORM_ZEPHYR=ON                   # Enable Zephyr platform
-DNNG_TESTS=OFF -DNNG_TOOLS=OFF -DNNG_ENABLE_TLS=OFF
```

## Build & Run: MQTT Demo

Async state-machine MQTT client. Follows [`demo/mqtt_async`](../demo/mqtt_async/mqtt_async.c) pattern.

```bash
cd demo/zephyr_mqtt
west build -b qemu_x86 .
west build -t run
```

Expected output:
```
=== NanoNNG Zephyr MQTT Client (async) ===
Broker: mqtt-tcp://broker.emqx.io:1883
MQTT: socket opened
MQTT: connected
MQTT: subscribing 2 topics
MQTT: event loop running...
MQTT RECV: 'hello' FROM: '/zephyr/msg/1'
MQTT SEND: 'hello' TO: '/zephyr/msg/transfer'
```

See [`demo/zephyr_mqtt/README.md`](../demo/zephyr_mqtt/README.md) for testing with MQTTX.

## QEMU Networking

Uses **QEMU User Networking (SLIRP)** — no host-side setup needed.

| Address     | Role                        |
|-------------|-----------------------------|
| `10.0.2.2`  | Host (gateway)              |
| `10.0.2.3`  | DNS proxy (QEMU SLIRP)      |
| `10.0.2.15` | Zephyr guest IP             |

## DNS Configuration

```kconfig
CONFIG_DNS_RESOLVER=y
CONFIG_DNS_SERVER_IP_ADDRESSES=y
CONFIG_DNS_RESOLVER_MAX_SERVERS=2
CONFIG_DNS_SERVER1="10.0.2.3"       # QEMU SLIRP DNS proxy
CONFIG_DNS_SERVER2="8.8.8.8"        # Fallback
```

**⚠️ Do NOT use `10.0.2.2` as DNS server** — use `10.0.2.3` (QEMU's built-in DNS forwarder).

## Platform Port Architecture

- **Threading**: Zephyr native `pthread`; EINVAL retry for lazy mutex registration; `NULL` attr for thread creation
- **Atomics**: `nni_atomic_u64` via `pthread_mutex` (32-bit x86 lacks `__atomic_store_8`)
- **File I/O**: Full POSIX when `CONFIG_FILE_SYSTEM=y`; `access()` emulated via `stat()`; returns `NNG_ENOTSUP` otherwise
- **DNS**: Synchronous `getaddrinfo` → `zsock_getaddrinfo`; no worker threads
- **Event loop**: `poll(fds, nfds, 100)` with 100ms timeout

## Kconfig Reference

| Option                            | Purpose                     |
|-----------------------------------|-----------------------------|
| `CONFIG_POSIX_API=y`              | POSIX threads, sockets, poll|
| `CONFIG_DYNAMIC_THREAD=y`         | Runtime thread creation     |
| `CONFIG_HEAP_MEM_POOL_SIZE=131072`| Heap (≥128KB)              |
| `CONFIG_NET_QEMU_USER=y`          | SLIRP NAT (no host setup)  |
| `CONFIG_DNS_SERVER1="10.0.2.3"`   | QEMU SLIRP DNS proxy        |
