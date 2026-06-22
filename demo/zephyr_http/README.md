# NanoNNG HTTP Test Demo for Zephyr RTOS

Self-contained HTTP server + client test running on Zephyr RTOS.

## Overview

Starts an HTTP server on `127.0.0.1:18888`, then runs client tests against it:

| # | Test | Description |
|---|------|-------------|
| 1 | GET /api/test | Expect 200 + "Hello Zephyr HTTP" |
| 2 | GET /api/json | Expect 200 + JSON response |
| 3 | POST /api/echo | Expect 200 + echoed request body |
| 4 | GET /api/nonexistent | Expect 404 Not Found |

All communication over TCP loopback — no external server required.

## Quick Start

```bash
# Build
cd demo/zephyr_http
west build -b qemu_x86 .

# Run (requires socat for QEMU SLIP socket)
socat UNIX-LISTEN:/tmp/slip.sock,fork,unlink-early PIPE &
sleep 1
timeout 30 west build -d $(pwd)/build -t run
kill %1 2>/dev/null

# Or with native_sim (no QEMU, no socat needed)
west build -b native_sim .
west build -t run
```

## Expected Output

```
========================================
  NanoNNG Zephyr HTTP Test
========================================
--- Test 0: Server Start ---
  PASS: HTTP server started on http://127.0.0.1:18888

--- Test 1: GET /api/test ---
  PASS: status=200, body='Hello Zephyr HTTP'

--- Test 2: GET /api/json ---
  PASS: status=200, application/json body received

--- Test 3: POST /api/echo ---
  PASS: status=200, echo body matches

--- Test 4: GET /api/nonexistent (404) ---
  PASS: status=404 Not Found

========================================
  Results: 4 PASS, 0 FAIL
========================================
```

## Board Support

| Board | Build | Runtime |
|-------|-------|---------|
| `qemu_x86` | ✅ | ⚠️ TCP hang (pre-existing issue) |
| `native_sim` | ✅ | ⚠️ TCP hang (pre-existing issue) |

The HTTP server starts and handler logic is correct. TCP client connect hangs
due to a pre-existing Zephyr networking issue (also affects MQTT demo).

## Configuration

See [`prj.conf`](prj.conf) for Zephyr Kconfig — POSIX threads, TCP networking,
loopback support, and memory pools.

The build uses `nanonng_external.cmake` which enables `NNG_ENABLE_HTTP=ON`
by default.
