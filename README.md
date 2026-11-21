<!--
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
-->

# Lichii (Custom CPU & GPU Optimization Fork)

<p align="center">
  <img src="dist/branding/lichii_icon_with_logo.png" alt="Lichii" width="500">
</p>

An enhanced, high-performance fork of **[zortos293/Lichii](https://github.com/zortos293/Lichii)**, engineered to deliver perfectly stable frame pacing, eliminate shader compilation stutter on 6-core/6-thread CPUs, and significantly reduce system RAM and GPU VRAM consumption.

---

## 🚀 Key Optimizations in this Build

### 1. Smart Shader Worker Scaling & Boot Turbo Mode (`GetSmartPipelineWorkers`)
- **Boot Turbo Acceleration:** During initial disk shader cache loading and precompilation (`LoadDiskResources`), guest CPU and audio threads are idle; the scheduler dynamically expands active concurrency (e.g., **5 CPU + 1 Driver** on 6C/6T CPUs) to cut initial shader loading time significantly.
- **Zero-Deadlock Gameplay Transition:** Once boot loading completes, `SetActiveWorkerLimits` lowers concurrency cleanly via `std::condition_variable` without tearing down threads, restoring the conservative gameplay profile (**2 CPU + 1 Driver** on 6C/6T CPUs such as Core i5-9400F, i5-8400, or Ryzen 3500) to prevent thread thrashing and protect guest CPU, audio, and Vulkan submission threads.
- Scalable dynamic profiles for 4-thread, 6-thread, 8-thread, 12-thread, and 16+ thread CPUs.

### 2. VIP Shader Queue & Batch Syscalls
- **4-Tier Priority Hierarchy:** `NeedNow` (blocking) -> `Vip` (essential gameplay) -> `Soon` (effects/particles) -> `Background` (precache).
- Shaders with depth-write enabled and compute shaders are automatically classified as **VIP**, ensuring ground, environment, and characters compile before transparent smoke or dust.
- **Syscall Batching (`BeginBatch` / `BatchScope`):** Coalesces rapid multi-shader dispatches into a single mutex acquisition and single `notify_all()` kernel syscall, eliminating context-switch storms.

### 3. Nx2FastDraw Same-Value Write Filter
- Prevents spurious FastDraw breaks when game macros rewrite the same viewport, blend state, or vertex buffer parameters with identical values (`if (regs.reg_array[method] == argument) return;`).
- Adds specialized routing for `IsStoreOnlyExecutable` registers, keeping FastDraw active across dense instances and foliage.

### 4. GPU ASTC & Unswizzle Compute Thresholds
- High-resolution textures and map streaming data ($\ge 4096$ bytes or $\ge 64\times 64$) are decoded and unswizzled in microseconds via GPU Compute Shaders (`astc_decoder.comp`).
- Small UI icons bypass GPU queue barriers and decode in nanoseconds on the CPU. Complete Vulkan memory barriers prevent texture flickering on first display.

### 5. Precision Hybrid Frame Pacing (`PresentManager` + Mailbox)
- **`VK_PRESENT_MODE_MAILBOX_KHR`:** Decouples the emulator from rigid FIFO VSync half-rate drops (no more 60 $\to$ 30 FPS drops on 0.1 ms misses).
- **Zero-Stall Mailbox & Auto Presentation:** In Mailbox VSync and Auto pacing modes, avoids redundant CPU spin-waits and artificial thread delays, delivering fresh frames immediately to the swapchain for maximum throughput on 6-core CPUs while keeping pacing telemetry accurately logged.
- **Hybrid `SleepUntil`:** For fixed target modes (30/60 FPS), uses kernel OS sleep with a trimmed 250 µs spin-wait to save CPU cycles.
- **Anti-Burst Protection:** Resets pacing cadence after loading screens or long hitches to prevent fast-forward frame bursts.

### 6. Asynchronous N-1 Occlusion Queries
- In `SamplesQueryBank::Sync`, occlusion queries (`ZPassPixelCount64`) return the previous frame's result when pending (`VK_NOT_READY`), eliminating 2–8 ms CPU stalls in the middle of a frame.
- Transform Feedback and physics queries remain strictly synchronous for 100% collision accuracy.

### 7. Guest CPU Busy-Polling Mitigation
- Automatically detects tight spin-loops in Dynarmic (`GetCNTPCT`) and yields the physical host CPU thread after 2,000 tight iterations.
- Detects `svcSleepThread(0)` and explicit yield SVCs to prevent idle guest threads from burning 100% of a physical CPU core.

### 8. VRAM Reduction, 3-Tier GC & Memory Architecture (Credits: Tasarren)
- **Direct VMA Memory Allocation:** Streamlines image creation directly through Vulkan Memory Allocator to eliminate probe/destruction overhead while preserving 4 MiB buffer blocks.
- **Dynamic VRAM Target (85% Total GPU VRAM):** Soft target dynamically scales device memory usage to 85% of total dedicated GPU VRAM (~6.95 GB on 8 GB cards, ~10.4 GB on 12 GB cards, ~13.9 GB on 16 GB cards), reserving ample head-room for Windows DWM and preventing system commit exhaustion.
- **Discrete GPU Memory Threshold Protection:** Distinguishes integrated GPUs from discrete GPUs so non-texture allocations never inadvertently inflate discrete GPU retention floors up to the physical memory limit.
- **3-Tier Texture Garbage Collector (Anti-Thrashing):**
  - **< 75% GPU VRAM:** Relaxed retention (90 frames, ~1.5–3s), preserving textures across corners and turns with zero re-decode thrashing.
  - **75%–85% GPU VRAM:** Moderate eviction (60 frames, 32 iterations/frame) for steady pruning of stale assets.
  - **> 85% GPU VRAM (Emergency):** High-throughput eviction (15 frames, 128 iterations/frame) to swiftly reclaim space before driver OOM limits.
- **Allocation & ShaderCacheWriter Memory Shield:** Protected with `try/catch` exception handlers in `JoinImages` and `CacheWriter` to ensure low-commit spikes or cache write delays never trigger process aborts.

### 9. Draw Submission Batching (`DefaultNativeBudget = 256`)
- Elevates native desktop submission batching from 128 to 256 draw calls, cutting Vulkan queue recording and submission frequency in half on 6-core CPUs while keeping discrete GPUs continuously supplied with work.
- Guarantees immediate flushing on swapchain present and key render target transitions.

### 10. Low-Latency Micro-Spin for GPU Readbacks (`WaitForSyncOperation`)
- Employs an ultra-short adaptive pause micro-spin loop prior to invoking the kernel condition variable wait during GPU-to-CPU read synchronization (`OnCPURead`).
- Eliminates Windows kernel context switches and thread descheduling when fast discrete GPUs complete transfers in microseconds, with reliable fallback to kernel waits if transfers take longer.

### 11. Multi-Entry Associative UBO Stream Cache (16-Way per Slot)
- Upgrades uniform stream buffer reuse with a 16-way associative history cache per slot indexed by `device_addr` and verified with fast 64-bit CityHash comparison.
- Eliminates repeated mapped staging allocations and memory copy overhead during round-robin draw call rendering across multi-actor scenes (e.g., 12-kart racing packs).
- Elevates `MissStreak` tolerance to 32 to prevent cache deactivation when distinct actor matrices alternate continuously within a frame.

### 12. GPU Front-End / Back-End Decoupling & Drain Elimination
- **1-Frame Ahead for Mario Kart World:** Enables the guest CPU to prepare and emit command lists for frame $N+1$ concurrently while the GPU concludes presentation of frame $N$, completely removing synchronous presentation fence stalls (`acquire`).
- **Zero-Stall Command List Snapshots:** Bypasses synchronous `WaitForSyncedWriters` stalls whenever command lists are already captured into host memory at submit time and are not overwritten by GPU compute.
- **Smart Queue Drain Bypass:** In `CheckWithoutDrain`, validates guest memory before triggering full pipeline flushes (`Drain()`), bypassing thousands of 1–2 ms queue drain freezes when memory already satisfies the semaphore acquire condition.

### 13. Intel Core Optimization (AVX2 Vectorization) & Link-Time Optimization (LTO)
- **Configurable AVX2 Code Generation:** Adds a dedicated checkbox toggle in **Emulation > Configure > CPU** (*"Intel Core Optimization (AVX2 Vectorization)"*) allowing users to enable or disable AVX2 JIT instruction emission on demand.
- **Startup Compatibility Reporting:** Automatically detects host CPU vector capabilities at boot and logs explicit AVX2 hardware compatibility and active profile status.
- **Link-Time Code Generation (LTO / LTCG):** Enables MSVC `/GL` compiler and `/LTCG` linker flags for Release builds, enabling aggressive interprocedural inlining across modules.

### 14. Shared Dynarmic JIT Code Cache (Credits: Tasarren)
- **Unified Multi-Core JIT Cache:** Shares a single unified 1 GiB JIT executable code cache across all guest CPU cores on x86-64 multicore, eliminating 72%–74% of redundant physics and AI recompilation during multi-actor scenes (e.g. 12-kart racing packs) and saving 800–900 MiB of system memory.
- **Lock-Free Fast Paths:** Protected with `std::shared_mutex` for zero-contention simultaneous lookups via `std::shared_lock` across all 6 cores and atomic double-checked block insertion on cache misses.

### 15. In-Memory Telemetry Architecture (10s Consolidated Reporting)
- Replaces high-frequency per-frame and per-readback disk log writes (`PaceToVblank`, `OnCPURead`, `RanFrameAhead`) with lock-free RAM counters using `std::atomic<u64>` (`std::memory_order_relaxed`).
- Emits a single consolidated `10s Telemetry` summary line every 10 seconds and upon session exit, eliminating log mutex contention and SSD I/O stalls during gameplay without sacrificing diagnostic metrics.

### 16. NVIDIA Global Memory Route Optimization (Credits: Tasarren)
- Restricts `OpSelect` global memory buffer selection (`SelectsGlobalMemoryBuffer`) strictly to compute shaders and the two specific graphics hashes requiring it (`0xf98012dc9a4465ff`, `0xe78efceda31f97a8`).
- Restores direct candidate buffer routing in graphics shaders on NVIDIA proprietary drivers, reducing instruction overhead and improving rendering throughput.

### 17. Software Video & AV1 Cutscene Pool (`FFmpeg` / `libdav1d`)
- Caps software video decoding worker threads based on host CPU topology (`thread_count = 2` on $\le 6$-thread CPUs, with `libdav1d` `framethreads`/`max_frame_delay` tuned to `2` and `tilethreads = 1` when no hardware decoder is active), preventing AV1 cutscenes from saturating guest CPU cores.
- Expands the decoded frame buffer queue (`MaxDecodedAhead = 5`) in `INx2VideoSession` to absorb software decode jitter and maintain smooth cutscene pacing.

### 18. Hybrid Staging Buffer Pool (`StagingBufferPool`)
- Retains upload staging buffers up to 32 MiB permanently (2 per level) while adding a transient 120-frame (~2 s) TTL retention window for giant upload buffers (64 MiB–512 MiB, covering 288 MiB texture uploads).
- Eliminates synchronous `vkCreateBuffer` / `vkAllocateMemory` stalls on consecutive large texture uploads without permanently pinning hundreds of MiB on 16 GB RAM systems.

### 19. Immediate Superseded Render Target Cleanup (`ClassifySuperseded`, Credits: Tasarren)
- Detects render targets and overlapping images whose guest memory range has been completely covered by newer cleared images (`full_write_tick`) at frame boundaries (`DeleteSupersededImages`), releasing their VRAM after 2 unused frames without waiting for the full GC cycle.

### 20. Memory-Aware Boot Shader Budget & Transient 2D Draw Heuristic
- **Dynamic Boot Shader Cap (`ComputeDynamicBootPipelineCap`):** Sizes cold-cache boot shader precompilation dynamically from available host RAM and GPU VRAM budget (clamped between 1,024 and 32,768 pipelines) while respecting `KeptPipelines()`.
- **Transient 2D Effect Heuristic (`CanDiscardTransient2DDraw`):** Allows asynchronous pipeline compilation for large or instanced transient 2D particle/effect draws into existing render targets without depth testing, while keeping persistent font atlas draws (30–672 vertices) strictly synchronous.

### 21. CPU Stutter Elimination & Frame Pacing Engine (Build 1.13)
- **ZeroTrim Exponential Backoff:** Tracks newly kernel zero-filled memory and trims only after active zero allocations exceed 64 MiB post-boot, backing off up to 300 s with an emergency wake-up when system commit headroom drops below 2 GiB. Eliminates ~29,000 periodic soft page faults and 1.0–1.6s CPU stalls every 11 seconds.
- **Dual-VSync Elimination for 30 FPS Mode:** Extends the `b.lo` to unconditional branch bypass on `vblank_wait_site` to fixed 30 FPS mode (`mkw_30fps`), establishing `PaceToVblank` (`swap_interval = 2`) as the sole authoritative clock and eliminating the 0.3 ms clock drift that caused 66.6–73.3 ms frame hitches.
- **8 MiB Stage Translation Texture Pool:** Expands `MaxFlushedTexturePoolBytes` from 4 MiB to 8 MiB, allowing 131,372-descriptor pools (e.g. Mario Kart World's 4.2 MB pool) to translate asynchronously on background shader workers instead of stalling `GPUBack`.
- **Lock-Free OnCPURead Fast Path:** Guards `texture_cache.mutex` acquisition in `FlushRegion` / `FlushRegionForRead` with `page_filter.Any(addr, size)` and increases `MaxReadBackPages` from 512 to 2048, eliminating mutex contention between guest CPU culling reads and texture submission.
- **Macro 0x142 DMA Page Cache:** Caches the last clean 4 KiB DMA page in `MacroEngine::Execute`, avoiding hundreds of redundant `FlushPending` / `IsMemoryDirty` lock acquisitions during 24-byte coordinate batch reads.
- **Abandoned Query Suppression:** Automatically suspends `StartCounter` / `EndCounter` when 4 consecutive 1024-slot batches of `SamplesPassed` queries are dropped without reading, immediately reactivating upon the guest's first `ResetCounter` / `WriteCounter`.

---

## 🎮 Recommended Settings Guide

Open **Emulation > Configure** (or `Ctrl+,`):

* **CPU:**
  - **Accuracy:** `Auto`.
  - **Intel Core Optimization (AVX2):** `Enabled` (for Intel Haswell+ and AMD Zen+ CPUs).
* **Graphics:**
  - **Resolution:** 1X (720p/1080p).
  - **VSync Mode:** `Mailbox` (configured by default for flat frametimes).
  - **Scaling Filter:** `ScaleForce` (superior edge sharpness without the ~1 ms overhead of FSR3 or bloom blow-up on water) or `Bilinear` (maximum CPU/GPU efficiency).
  - **Anti-Aliasing Method:** `None` (preserves distant sharpness and prevents texture blurring caused by FXAA at native 1080p).
  - **ASTC Recompression:** `BC3` (or Uncompressed if VRAM $\ge 8$ GB).
  - **ASTC Decode Method:** `GPU`.
* **Advanced Graphics:**
  - **Frame Pacing Mode:** `Target Auto` or `Target 60`.
  - **Async Shader Compilation:** `Enabled`.
  - **Disk Shader Cache:** `Enabled`.

---

## 👥 Credits and Acknowledgements

This custom optimization fork builds upon the incredible work of the open-source community:

- **[Zortos (zortos293)](https://github.com/zortos293/Lichii):** Creator and main maintainer of Lichii.
- **[Tasarren](https://github.com/tasarren/Lichii):** Author of the VMA dedicated render target/storage pool optimizations, shared Dynarmic JIT code cache, NVIDIA global memory route isolation, and VRAM reduction techniques.
- **Upstream Projects:** Eglantine (Camille LaVey), Eden Emulator Project, yuzu, and Sudachi.

---

## License

Lichii is licensed under the **GNU General Public License v3.0 or later** (GPL-3.0-or-later). See [LICENSE.txt](LICENSE.txt) for details.
