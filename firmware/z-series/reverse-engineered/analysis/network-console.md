# Core diagnostic orchestration and BinaryDownload self-test

The reconstructed Network, Int and All console bodies execute the original
marker, network selection, diagnostics and child-call order. The reconstructed
BinaryDownload test uses the original 16-byte test specialization and completes
both eight-byte writes before checking completion. The production downloader
remains a separately verified 2,048-byte receiver.

## Original execution evidence

`network-console-validation.json` records **64 exact cases**, **33 original
function bodies** and **148 executions of the real returning assertion helper**.
The helper increments the shared counter, delays 200 ms and returns. Fixtures
cover normal and wrapping clocks/counters, both interface choices, missing
network, missing client, missing client class, child delays and child assertions.
Eight additional native contract cases require missing or failed services to
propagate failure.

| Original entry point | Reconstructed owner |
| --- | --- |
| `401024a8` | `binary_download_console_test` |
| `4011bffc`, `4011c2f8` | `core_network_console_self_test` |
| `4010a364`, `4010a3d8` | `core_integration_console_self_test` |
| `4011fbbc` | `core_all_console_self_test` |

The Network body reads actual interface addresses, prefers Ethernet, records the
chosen interface and calls Azure, DownloadBinary, Pinger, HttpClient and
HttpBackend in the original order. Its client-factory assertions continue after
the original helper returns. Int preserves the warning and MSP integration
call. All preserves Basic, Int and Network ordering. Registered wrappers and
`ApplicationConsole::invoke_original` route to these portable owners.

## Boundaries and limits

The orchestration replay uses explicit typed boundaries for the child test
bodies. These boundaries record the exact original callback address and model
specified elapsed time and returning assertions. This proves the aggregate
ordering and its observations; it does not claim a single emulation run through
all nested test suites. Separate full leaf comparisons establish the child
bodies, and the SDK composition binds the actual implementations.

Network addresses and client allocation are prescribed environment results.
The original factory's defined return paths make its Boolean result and output
client presence agree. The native interface preserves that contract. Diagnostic
rendering, SDK clock and the separately reconstructed Head message renderer are
also explicit boundaries.

The BinaryDownload test's original local payload is uninitialized and never
inspected. The reconstruction supplies zero bytes, preserving byte counts,
copies and successful completion. The proof compares all observable state and
diagnostics, not those undefined payload bytes. No live cloud or hardware
operation is performed by these comparisons.

Fixtures are deterministic gzip files. `run_network_console_emulation.py`
regenerates them from the preserved, byte-verified image in a read-only Ghidra
project. `validate_network_console.py` performs the native comparison and records
the exact source hashes. `build_network_console_objects.py` uses the pinned
ESP32 compiler; a complete application link is a separate integration gate.
