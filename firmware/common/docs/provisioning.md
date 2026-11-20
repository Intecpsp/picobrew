# Physical-console provisioning

`Provisioning` owns no serial port and never echoes input. A machine feeds at
most its serial-byte budget per loop and prints only the returned
`Result.message`. Existing console dispatchers must give this parser an
explicit, exclusive provisioning mode; do not send the same input to two
independent command consumers.

With the appliance idle, send one LF- or CRLF-terminated line:

```
key <64 lowercase hexadecimal characters>
wifi <SSID><TAB><password>
status
```

`<TAB>` means one literal tab between SSID and password, so spaces in either
value remain intact. The SSID is 1–32 bytes with no control characters. An open
network has an empty password; WPA passwords contain 8–63 printable ASCII
characters, or a 64-character hexadecimal PSK. The key is explicitly supplied,
never defaulted or inferred. Neither credentials nor key are included in
acknowledgements or status text. Lines exceeding the bounded 160-byte buffer
are discarded through their newline, and the buffer is cleared after dispatch.

The runtime loads verified `api.key` and versioned two-bank `network` records
from its separate modified namespace, updates the HTTP server key only after a
verified key write, and changes Wi-Fi only after a verified network write.
Acknowledgement means saved; it never claims the Wi-Fi association succeeded.
The ordinary browser preferences route contains neither key nor credentials.

A model may import its verified original credential record read-only only
when the modified network record is definitely absent. An unreadable/corrupt
modified record blocks import. No erase or filesystem-format command is
provided. Explicit physical provisioning can replace the requested value;
read-only startup and status cannot.

`tests/test_provisioning.py` runs the real parser and record layer under
ASan/UBSan with short/failed/corrupt writes, restart, input overflow, busy-state
guards, read-only OEM import and secret-free acknowledgements.
