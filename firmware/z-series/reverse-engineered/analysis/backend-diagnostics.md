# Backend assertion and diagnostic bindings

The application can now bind the recovered `HttpBackend`, `ZBackend` and
`CoreHelpers` callbacks to their actual source file, source line, assertion
expression, log tag, severity and message. `backend_diagnostics.hpp` exposes
typed lookup and rendering functions; it does not replace the shared returning
assertion implementation or the platform timestamp renderer.

The mapping covers 29 assertion sites and 42 diagnostic sites. All 51 distinct
assertion/message literals are checked directly against the preserved Z119
image. The native probe checks 175 lookup, forwarding and formatting cases,
including all six ArduinoJson error names, signed dropped-byte counts, recipe
list capacity, failed HTTP methods, command names, backend errors, and all
session logger state names. Missing callbacks, unknown lines, absent required
arguments and invalid JSON error values fail explicitly.

`HttpBackend` retains the method/command text and dropped-byte count needed by
its line-only callback. `ZBackend` retains the JSON parser error and dropped-byte
count. Those values are set before invoking the diagnostic callback, so the
application does not infer a message from a later state. The existing 732-case
backend execution replay remains unchanged: 730 exact results and two documented
OEM stack-canary abort differences for ten-digit session IDs.

The registration stop trace (`CoreHelpers.h:0xca`) is an original paired
`ScopeLogger` event. It has a separate required scope binding because an ordinary
debug log cannot reproduce the entry/exit prefixes and elapsed-time field.
An absent scope implementation returns failure instead of emitting an invented
generic message.

`backend-diagnostics-validation.json` records literal addresses and native
expectations. Its evidence is source/literal recovery plus native contract
testing, not a claim of fresh execution of every original printf call. Full
original backend/helper control-flow comparisons are recorded separately in
`backend-services-validation.json` and `core-helpers-validation.json`.
`backend-diagnostics-target-build.json` pins the one owned diagnostic object
from the successful 86-module compilation with the selected ESP32 toolchain.
No live cloud, physical device or complete application link is claimed here.
