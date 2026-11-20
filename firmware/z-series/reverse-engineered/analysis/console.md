# Console dispatch, help and serial command ownership

`console.hpp/cpp` reconstructs the templated OEM Shell behavior with
caller-owned storage. Commands keep registration order; the first case-sensitive
exact/prefix match wins. The callback receives the untrimmed suffix after its
token. Child shells receive that same suffix. Registered `?`/`??` triggers take
precedence over the built-in help fallback, which itself accepts prefixes.
The complete recursive help renderer preserves the two 128-byte formatting
steps, field padding, descriptions, child labels and original diagnostic lines.

Registration executes all applicable returning assertions. An empty
description is diagnosed but does not prevent an otherwise valid registration.
Capacity overflow, an empty trigger or a null child are diagnosed and rejected;
the CString diagnostic copy and its warnings are retained. Null callbacks and
null dispatch inputs are explicit rebuild guards, and recursive shell graphs
are limited to 64 levels instead of reproducing stack exhaustion. Those guards
are not presented as original behavior.

`serial_console.hpp/cpp` implements the 128-byte serial reader and one-slot text
mailbox. NUL, CR and LF terminate a line. Filling the buffer flushes it without
consuming the next byte. One invocation stops after the first completed line;
partial input remains for the next tick. Any nonprintable byte invalidates the
whole line, and an empty line is diagnosed. Frozen-buffer assertions execute
their continuation. A pending mailbox retains the first command and diagnoses
the discarded replacement. Both the mailbox and its consumer own their text;
no queued pointer refers to a reader buffer that is subsequently reset.

The ESP32 adapter uses the actual Arduino Stream/Print interfaces and the shared
diagnostic counter/delay service. Stream, output, diagnostic state and mailbox
bindings are required, application-owned objects. Final Core assembly must
dispatch the mailbox at the original tick position.

## Evidence and limits

`console-validation.json` records **554 matching original/native cases**.
The oracle executes Shell registration, dispatch, complete help, CString
rejection-copy diagnostics, SerialReader, text construction, enqueue/drop and
the real assertion helper through their returns. Coverage includes all 256
input byte values, overlapping and nested commands, recursive/overridden help,
long-prefix truncation, empty descriptions, capacity limits, frozen/full
buffers, retained incomplete lines, occupied queues, timestamp rounding and
counter/timer wraparound.

The explicit boundaries are ROM memory/string operations, simple CString
matching/null tests, C++ callable storage/invocation, stream I/O, printf-family
formatting, float helpers, time/delay and single-thread memory ordering.
Application results and ordered message bodies are compared; no serial timing,
heap allocation equivalence, concurrent access or physical hardware claim is
made. The target report records the concrete adapter compilation against the
pinned ESP32 toolchain. The gzip replay fixture has a deterministic timestamp
and records every compared input/result.

```sh
python3 tools/validate_console.py --prepare-input /tmp/console-cases.json
# Run tools/ghidra/EmulateConsole.java with the input and output JSONL paths.
python3 tools/validate_console.py --original-jsonl /tmp/console-original.jsonl \
  --export /path/to/z119-callback-export --write-evidence
python3 -m unittest discover -s tests -p test_console.py -v
```
