# Z119 remote-session dispatch into Head

`head_remote_session` reconstructs Core `0x401021b8`. It respects the original
unsigned time difference from the last encoder activity and the configured
quiet interval, then routes accepted static programs to the Head Azure queue.
Coffee programs preserve the original conversions to liters and Fahrenheit,
with time and intensity units normalized to seconds and intensity. Buffered
program 24 creates the corresponding owned Zymatic, PicoPak or BrewPen recipe
command; other requests are discarded through the original Azure service path.

All **164 original execution cases** match. Coverage includes every program ID
from 0 through 29, all ten tested session types for buffered recipes, empty and
64-byte names, accented text, exact quiet-period boundaries and unsigned timer
wraparound, all four coffee programs, incompatible quantity units and invalid
command/program assertions. The assertions execute the real OEM helper and
compare exact source/expression text, counter changes and ordered 200 ms delays.

The callback boundaries are explicit: Core and Head queue puts and Azure
session discard. `HeadApplication` supplies the actual single-slot mailbox and
owned Core envelope; payload copies rebase `CoreCommand::detail` to their own
storage. The original final `Core.session.use_azure` clear is retained in the
reference result and performed by the containing Core application.

ROM memory/string calls, compiler arithmetic, prescribed clock/log/delay and
windowed calling conventions remain explicit execution boundaries. The pinned
ESP32 object build and native tests do not establish physical network behavior
or an installed complete firmware image.

```
python3 tests/run_head_remote_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_remote.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_remote.py -v
```
