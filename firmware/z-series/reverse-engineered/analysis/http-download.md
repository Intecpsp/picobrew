# Firmware update and binary download receivers

`http_download.hpp/cpp` reconstructs the original Z119 `HttpUpdate` and
`DownloadBinary` receiver callbacks. Their `HttpReceiver` adapters bind directly
to the reconstructed outgoing HTTP client. The application owns each receiver
for the request lifetime; the types cannot be copied while callbacks refer to
them. SDK adapter failure is latched and prevents later callbacks from reporting
completion or triggering a restart.

The firmware updater requires the exact `application/octet-stream` content
type, calls the Arduino Update API for the declared size, streams response
chunks, calls `Update.end(false)`, and checks SDK progress before logging the
MD5 and restarting. Its concrete ESP32 adapter uses the pinned SDK's Update,
MD5 and restart implementations. The OEM counts supplied bytes even when
`Update.write()` reports a short write; final SDK completion/error checks remain
responsible for rejecting the failed update. Error state is sticky until the
owning Core resets status for a new transfer.

The binary receiver stores at most 2,048 bytes. Its signed header-length test
accepts a negative/unknown length, as the original does. Oversized body chunks
set an error without copying. A completion length mismatch logs the actual
returning assertion, increments the shared counter, delays, and then sets the
complete flag. A corrupt starting index above the buffer capacity and invalid
source spans are explicitly rejected by this reconstruction; the OEM can
otherwise underflow its capacity calculation and write beyond the buffer.
These guards are not claimed to match that unsafe memory behavior.

## Evidence

`http-download-validation.json` records **425 matching original/native cases**
covering all six callback bodies, complete accepted transfers, rejected headers,
sticky errors, short SDK writes, failed/unfinished updates, full/overflowing
buffers, unsigned byte-count wraparound and assertion continuation. It records
the whole binary buffer hash as well as ordered SDK calls and diagnostics.

The oracle executes the original receiver and assertion instructions. SDK
Update state/operations, MD5 string lifetime, restart, ROM memory/string calls,
formatted logs and time are explicit boundaries. The target build confirms
the concrete adapter compiles against the pinned historical SDK. No operation
in this validation flashes a machine or verifies an installation layout.

```sh
python3 tools/validate_http_download.py --prepare-input /tmp/download-cases.json
# Execute tools/ghidra/EmulateHttpDownload.java with input/output JSONL paths.
python3 tools/validate_http_download.py --original-jsonl /tmp/download-original.jsonl \
  --export /path/to/z119-callback-export --write-evidence
python3 -m unittest discover -s tests -p test_http_download.py -v
```
