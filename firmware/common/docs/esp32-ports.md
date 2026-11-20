# ESP32 standalone platform ports

`platform/esp32` supplies shared ports for Z-series and PicoStill. The HTTP
listener and telemetry client use nonblocking lwIP sockets. Telemetry DNS is
queued onto the TCP/IP thread; a cancelled lookup retains its owner and hostname
until the callback returns. A pending lookup prevents reuse of that owner.
There is no foreground DNS or connect wait.

The SPIFFS adapter mounts an existing partition with
`format_if_mount_failed = false`. It distinguishes a missing path from an I/O
error, checks all callback results, refuses rename over an existing destination,
and consumes each logical handle exactly once. The shared `VerifiedStorage`
layer supplies verified temporary writes and recoverable replacement.

The pinned ESP32 SPIFFS VFS does not implement `fsync`. Its `SPIFFS_close` flushes
the file write cache. The adapter therefore implements sync with a checked close,
a reopen without truncation, and restoration of the current offset. Subsequent
readback verifies the complete replacement. This does not establish physical
flash durability under every power-loss timing.

`python -m unittest discover -s firmware/common/tests -p test_platform_ports.py -v`
compiles the production ports against explicit SDK boundaries. It exercises
real host loopback sockets, queued and cancelled DNS callbacks, bounded empty
reads, and POSIX file handles with injected stat/close failures. It checks that
mount never formats, descriptor zero is valid, sync preserves data and offsets,
rename cannot replace an existing destination, and failed close cannot consume
a later handle. Pinned SDK compile and complete link evidence belong to each
machine build. Host tests are not hardware or flash installation evidence.
