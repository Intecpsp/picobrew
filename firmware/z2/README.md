# PicoBrew Z2

Upstream firmware is archived under [`../z-series/oem/`](../z-series/oem/).
That upstream grouping does not establish which image is compatible with this
configuration, so no image is relabeled as a verified PicoBrew Z2 dump.

Recovered source and standalone development are implemented under
[`../z-series/`](../z-series/README.md). This directory remains a physical-model
qualification boundary, rather than a duplicate source tree. A generic family
build does not identify a Z2 revision; `MODEL=z2` remains unavailable until an
original device backup establishes the mapping.
