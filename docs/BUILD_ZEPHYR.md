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

The demos in this project target `qemu_x86`, `native_sim`, and `mps2/an385` (ARM Cortex-M3) boards. Any Linux distribution supported by Zephyr (Ubuntu, Fedora, Debian, etc.) should work.

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

#### Install socat (required for QEMU SLIP)

The Zephyr QEMU runner uses a SLIP serial socket (`/tmp/slip.sock`) for networking.
`socat` must be installed to create this socket before running QEMU:

```bash
# Inside container
apt-get update && apt-get install -y socat

# Or on host
sudo dnf install -y socat        # Fedora
sudo apt-get install -y socat    # Debian/Ubuntu
```

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

A minimal PAIRv0 test over inproc transport with security-fix verification tests.
No networking required — runs on any Zephyr board with POSIX API support.

### QEMU Run Environment

All QEMU-based targets require a SLIP serial socket for the Zephyr network stack.
Create it with `socat` before running:

```bash
socat UNIX-LISTEN:/tmp/slip.sock,fork,unlink-early PIPE &
```

This keeps the socket alive until you `kill %1` or exit the shell.

### Quick Start (Docker)

```bash
# Enter container and activate Zephyr venv
docker exec -u root zephyr-tap bash
source /opt/python/venv/bin/activate

# --- qemu_x86 (fast, x86 QEMU) ---
cd /workdir/NanoNNG/demo/zephyr
west build -b qemu_x86 .
socat UNIX-LISTEN:/tmp/slip.sock,fork,unlink-early PIPE &
sleep 1
timeout 30 west build -t run
kill %1 2>/dev/null

# --- native_sim (fastest, no QEMU) ---
west build -b native_sim .
west build -t run

# --- mps2/an385 (ARM Cortex-M3, 4MB RAM; slow emulation) ---
west build -b mps2/an385 .
socat UNIX-LISTEN:/tmp/slip.sock,fork,unlink-early PIPE &
sleep 1
timeout 300 west build -t run
kill %1 2>/dev/null

# --- qemu_cortex_m3 (build only — 64KB RAM insufficient at runtime) ---
west build -b qemu_cortex_m3 .
```

### Per-Board Summary

| Board | Build | Run | Notes |
|-------|-------|-----|-------|
| `qemu_x86` | ✅ | ✅ | Full speed; `-march=atom` for inline 64-bit atomics |
| `native_sim` | ✅ | ✅ | Fastest; no QEMU, no SLIP socket needed |
| `mps2/an385` | ✅ 4.6% RAM | ✅ (slow) | ARM Cortex-M3; 4MB RAM; ~10s per socket op; needs socat |
| `qemu_cortex_m3` | ⚠️ 80% RAM | ❌ | TI LM3S6965 64KB SRAM cannot fit NanoNNG heap |

Expected output:
```
=== NanoNNG Zephyr - Security Fix Tests ===
--- Test 1: Basic Inproc ---
  PASS: nng_pair0_open(s1)
  PASS: nng_pair0_open(s2)
  PASS: nng_listen
  PASS: nng_dial
  PASS: nng_send
  PASS: nng_recv('hello', 6 bytes)
--- Test 2: Socket Lifecycle (30 cycles) ---
  PASS: socket create/destroy cycle
--- Test 3: Message Stress (100 msgs) ---
  PASS: message send/recv stress
--- Test 4: Batch Pair Lifecycle ---
  PASS: batch pair lifecycle
========================================
  Results: 9 PASS, 0 FAIL, 0 TESTS FAILED
========================================
```

### Board-Specific Configuration

Board-specific Kconfig fragments live in `boards/<BOARD>.conf` and are
loaded automatically by the Zephyr build system. The common `prj.conf`
contains only platform-independent settings.

| File | Purpose |
|------|---------|
| `prj.conf` | Common config (POSIX API, threading, heap, networking headers) |
| `boards/qemu_x86.conf` | Disable SLIP (inproc doesn't need networking) |
| `boards/native_sim.conf` | Pre-allocated thread pool for POSIX arch, larger heap |
| `boards/mps2_an385.conf` | ARM Cortex-M3 (4MB RAM): test entropy, increased mutex pool (128) |
| `boards/qemu_cortex_m3.conf` | Minimal config; builds but **cannot run** (64KB RAM insufficient) |

## Build & Run: MQTT Demo

Async state-machine MQTT client. Follows [`demo/mqtt_async`](../demo/mqtt_async/mqtt_async.c) pattern.
Requires TCP networking — board must support `CONFIG_NET_TCP`.

### Quick Start (Docker)

```bash
docker exec -u root zephyr-tap bash
source /opt/python/venv/bin/activate

# --- qemu_x86 with SLIRP user networking (built-in NAT) ---
cd /workdir/NanoNNG/demo/zephyr_mqtt
west build -b qemu_x86 .
socat UNIX-LISTEN:/tmp/slip.sock,fork,unlink-early PIPE &
sleep 1
timeout 30 west build -t run
kill %1 2>/dev/null

# --- native_sim (uses host network directly) ---
west build -b native_sim .
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

### Board-Specific Configuration

| File | Purpose |
|------|---------|
| `prj.conf` | Common MQTT config (POSIX API, TCP, DNS resolver) |
| `boards/qemu_x86.conf` | QEMU SLIRP NAT, static IP 10.0.2.15, DNS 10.0.2.3 |
| `boards/native_sim.conf` | Host networking, DNS 8.8.8.8 |

To add a new board, create `boards/<BOARD>.conf` with board-specific
network/DNS settings. No CMakeLists.txt changes are needed.

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
- **Atomics**: C11 `stdatomic` on x86/64-bit; pthread-mutex fallback (`NNG_ZEPHYR_NO_STDATOMIC`) on 32-bit ARM Cortex-M which lacks native 64-bit atomics. Each atomic struct embeds a `pthread_mutex_t`; cleanup paths call `nni_atomic_fini*` to release mutex pool slots.
- **File I/O**: Full POSIX when `CONFIG_FILE_SYSTEM=y`; `access()` emulated via `stat()`; returns `NNG_ENOTSUP` otherwise
- **DNS**: Synchronous `getaddrinfo` → `zsock_getaddrinfo`; no worker threads
- **Event loop**: `poll(fds, nfds, 100)` with 100ms timeout

## Kconfig Reference

| Option                            | Purpose                     |
|-----------------------------------|-----------------------------|
| `CONFIG_POSIX_API=y`              | POSIX threads, sockets, poll|
| `CONFIG_DYNAMIC_THREAD=y`         | Runtime thread creation     |
| `CONFIG_HEAP_MEM_POOL_SIZE=131072`| Heap (≥128KB)               |
| `CONFIG_MAX_PTHREAD_MUTEX_COUNT=128`| ARM: extra slots for mutex-fallback atomics |
| `CONFIG_TEST_RANDOM_GENERATOR=y`  | Entropy on boards without hardware RNG |
| `CONFIG_NET_QEMU_USER=y`          | SLIRP NAT (no host setup)   |
| `CONFIG_DNS_SERVER1="10.0.2.3"`   | QEMU SLIRP DNS proxy        |
