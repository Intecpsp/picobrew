# Pico S/Pro

| Directory | Contents |
| --- | --- |
| `oem/device/` | Verified 0.1.34 application image extracted from the Pico Pro backup |
| `oem/upstream/` | Unmodified files from the pinned Chiefwigms firmware archive |
| `reverse-engineered/` | Exact buildable OEM reconstruction before our v1.x features, from commit `ea887709e72c493456583114034cde4aad746099` |
| `modified/` | Our current v1.x firmware, device website, recipe data, packages and corresponding tests |

Both source folders retain their version-specific build and device tools under
`tools/`, their dependency manifests, tests and documentation. They can be built
from their own directory. General repository tooling lives at root `tools/`.

The pre-v1.x reconstruction retains its documented buffer, I/O and allocator
repairs; it is not recovered original vendor source or a byte-identical OEM
rebuild. Its earlier `legacy/` directory is decompiler evidence. The exact
snapshot is checked by `source-manifest.json`; develop new features in
`modified/`.

## Build from the repository root

```sh
make firmware MACHINE=pico-s-pro
make firmware-test MACHINE=pico-s-pro
make oem MACHINE=pico-s-pro
```

For the detailed current status and installation instructions, start with
[modified firmware](modified/README.md). The [OEM reconstruction README](reverse-engineered/README.md)
records its own evidence and limits. The Hub server is at root [`server/`](../../server/).
