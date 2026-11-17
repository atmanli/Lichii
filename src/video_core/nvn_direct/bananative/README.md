# Banana Native's NVN front end and Vulkan replay

The NVN2 direct renderer (`src/video_core/nvn_direct`) runs a game's NVN calls through
these files instead of the NVN driver and Lichii's Maxwell GPU path.

Origin: Banana Native (https://github.com/hugorosario/BananaNative, by hugorosario), the
Android port of nxnative (https://github.com/seedds/nxnative, by seedds). MIT, see
`LICENSE`; every imported file carries a line naming its origin.

Imported from upstream d154da1 (Banana Native v0.1.1) plus the lab's x86-64 Linux port and
its Mario Kart World fixes (lab branch nvn-direct-lab, 1877f5d: x86-64 port, ASTC through a
host transcode hook, NVN2 calls MKW makes, the empty-recording fix, NVN queues as Vulkan
queues, pass diagnostics, texture hooks). Paths below `dm/` are upstream's.

Not imported: `nvn_dirty.c` (Lichii's write tracking replaces it), `vka_replay.c`,
`vka_driver.c`, `vka_present.c`, `vka_framegen.c` and `lsfg/` (frame generation, GPL),
the Metal and Apple files, upstream's tests and tools.
