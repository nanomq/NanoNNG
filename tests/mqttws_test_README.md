# MQTT WebSocket Transport Unit Tests

## Overview

This test suite (`mqttws.c`) provides comprehensive unit tests for the MQTT WebSocket transport layer modifications in `src/sp/transport/mqttws/nmq_websocket.c`.

## Test Coverage

The test suite validates the following changes made to the MQTT WebSocket transport:

### 1. `wstran_pipe_send_cb` Function Tests

Tests the send callback handler with improvements including:
- **Error handling**: Verifies proper error detection and logging
- **Message cleanup**: Ensures messages are freed on send errors
- **Thread safety**: Validates mutex protection for shared state
- **Closed pipe handling**: Tests behavior when pipe closes during send

**Test Scenarios:**
- Successful send completion
- Send failure with various error codes (ECLOSED, ETIMEDOUT, ECONNRESET)
- Pipe closed during send operation
- NULL user_txaio handling
- Message ownership and cleanup
- Mutex protection during state updates

### 2. `wstran_pipe_recv_cancel` Function Tests

Tests the receive cancellation handler with thread safety improvements:
- **Mutex protection**: Validates all operations are mutex-protected
- **Early return logic**: Tests non-matching AIO handling
- **State management**: Ensures proper user_rxaio cleanup

**Test Scenarios:**
- Canceling matching receive operation
- Canceling non-matching receive operation
- Thread-safe cancellation with mutex locks
- NULL user_rxaio handling
- Multiple cancel attempts
- Mutex release on early return

### 3. Error Handling and Logging Tests

Validates improvements to error handling:
- Various error code handling (ECLOSED, ETIMEDOUT, ECONNRESET)
- Message cleanup on errors
- Logging of send errors with `log_warn`

### 4. Race Condition and Edge Case Tests

Tests for concurrent access scenarios:
- Pipe closing during send callback
- Message ownership between txaio and user_txaio
- Concurrent cancel and completion operations
- Mutex serialization of operations

### 5. Integration Tests

End-to-end flow testing:
- Send completion followed by receive cancel
- Multiple operation coordination
- State consistency across operations

### 6. Regression Tests

Validates specific bug fixes:
- **Variable declaration fix**: Tests that `rv` variable is accessible throughout function
- **Duplicate line removal**: Verifies single `nni_aio_set_msg` call in send_start_v4
- **Mutex protection addition**: Confirms recv_cancel is fully mutex-protected

## Changes Tested

### Bug Fix 1: Thread Safety in `wstran_pipe_send_cb`
**Before:**
```c
nni_mtx_lock(&p->mtx);
taio = p->txaio;
uaio = p->user_txaio;
// rv declared inside lock
```

**After:**
```c
int rv;
// ...
taio = p->txaio;
uaio = p->user_txaio;
nni_mtx_lock(&p->mtx);
rv = nni_aio_result(taio);
```

**Tests:** Validates variable scope and error handling improvements.

### Bug Fix 2: Error Handling in `wstran_pipe_send_cb`
**Added:**
```c
if (nni_aio_result(taio) != 0) {
    log_warn(" send aio error %s", nng_strerror(rv));
    nni_msg_free(nni_aio_get_msg(taio));
}
```

**Tests:** Verifies error logging and message cleanup on failures.

### Bug Fix 3: Mutex Protection in `wstran_pipe_recv_cancel`
**Before:**
```c
wstran_pipe_recv_cancel(nni_aio *aio, void *arg, int rv)
{
    ws_pipe *p = arg;
    if (p->user_rxaio != aio) {
        return;
    }
    // ...
}
```

**After:**
```c
wstran_pipe_recv_cancel(nni_aio *aio, void *arg, int rv)
{
    ws_pipe *p = arg;
    nni_mtx_lock(&p->mtx);
    if (p->user_rxaio != aio) {
        nni_mtx_unlock(&p->mtx);
        return;
    }
    // ...
    nni_mtx_unlock(&p->mtx);
}
```

**Tests:** Validates complete mutex protection for thread-safe cancellation.

### Bug Fix 4: Duplicate Line Removal in `wstran_pipe_send_start_v4`
**Before:**
```c
send:
    nni_aio_set_msg(aio, msg);
    nni_aio_set_msg(p->txaio, msg);
```

**After:**
```c
send:
    nni_aio_set_msg(p->txaio, msg);
    nni_aio_set_msg(aio, NULL);
```

**Tests:** Verifies correct message ownership transfer.

### Bug Fix 5: Logging Improvement in `wstran_pipe_send_start_v5`
**Before:**
```c
nni_println("ERROR: packet id duplicates in nano_qos_db");
```

**After:**
```c
log_warn("ERROR: packet id duplicates in nano_qos_db");
```

**Tests:** Indirectly validated through error handling tests.

## Test Framework

The tests use the Convey testing framework, which provides:
- BDD-style test organization
- Nested test contexts
- Automatic setup/teardown
- Clear test output

## Running the Tests

```bash
# Build and run all tests
mkdir build && cd build
cmake -DNNG_TESTS=ON ..
make
ctest -R mqttws -V

# Or run directly
./tests/mqttws -v
```

## Test Structure

Each test follows this pattern:
1. **Setup**: Create test pipe and AIOs
2. **Execute**: Perform operation under test
3. **Verify**: Assert expected behavior
4. **Cleanup**: Free resources

## Key Testing Principles

1. **Isolation**: Each test is independent
2. **Coverage**: Tests cover happy paths, edge cases, and error conditions
3. **Thread Safety**: Validates mutex protection
4. **Resource Management**: Ensures proper cleanup
5. **Regression Prevention**: Tests specific bug fixes

## Future Enhancements

Potential areas for additional testing:
- Performance testing under load
- Stress testing with many concurrent operations
- Fault injection testing
- Integration with real WebSocket connections
- MQTT protocol-specific scenarios

## Contributing

When modifying the MQTT WebSocket transport:
1. Update existing tests if behavior changes
2. Add new tests for new functionality
3. Ensure all tests pass before submitting changes
4. Document any test changes in this README

## Related Files

- **Source**: `src/sp/transport/mqttws/nmq_websocket.c`
- **Header**: `include/nng/transport/mqttws/nmq_websocket.h`
- **Tests**: `tests/mqttws.c`
- **Test Config**: `tests/CMakeLists.txt`

## References

- [Convey Testing Framework](https://github.com/gdamore/c-convey)
- [NNG Documentation](https://nng.nanomsg.org/)
- [MQTT Protocol Specification](https://mqtt.org/mqtt-specification/)