# 2026-09-24 Local API test hangs during shutdown

## Finding

CTest stops making progress at `tts_host_local_api`. The regression test in
`tests/local_api_server_tests.cpp` constructs the server with port `0`, allowing
the operating system to choose an available loopback port, then destroys the
server after its HTTP requests complete.

## Analysis

`src/local_api_server.cpp` retained the requested endpoint with port `0` after
`bind`. Its `port()` method returned the actual bound port, so test clients
reached the server correctly. During destruction, however, the worker thread
was woken by a connection to the retained endpoint; connecting to port `0`
failed. The worker remained blocked in `accept`, and destruction waited
forever in `join()`.

## Fix

After binding, store the acceptor's actual local endpoint. The destructor's
wake connection then reaches the listener even when the caller requested an
ephemeral port. Give this CTest a 10-second timeout so a future shutdown
regression fails with a bounded wait rather than stalling the rest of the
suite (`CMakeLists.txt`).

The loopback and explicit-origin policy remains as described in the
[local API design](../design/architecture.md#local-api) and
[ADR 0003](../adr/0003-no-authentication.md).

## Verification

Source inspection identified the port-0 mismatch in the existing regression
test. Tests were not run for this fix. On Windows, verify with:

```powershell
cmake --build build --config Debug --target tts-host-local-api-tests
ctest --test-dir build -C Debug -R "^tts_host_local_api$" --output-on-failure
```
