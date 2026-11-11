# Local validation, 2026-11-10

## Integrated candidate

The later integrated source `9d71cf868a14d6217442702ddb53a3f47626580d`
passed 34 quick cases / 4,381 assertions and both CTest checks, including the
empty-selection contract. This contribution retains byte-identical production,
test and replay-link files from that candidate; documentation changes are separate.
The same four-job MSVC/Ninja/ccache loop measured a 0.094 s no-op with zero compiler
calls and a 0.332 s forced unchanged rebuild with 5/5 direct cache hits.

[The Windows candidate run](https://github.com/FrsrG/Lichii/actions/runs/42926072741)
passed the complete build, import check and 10/10 CTest entries using clang-cl
22.1.8, Qt 6.8.3 and Windows SDK 10.0.26100.0. Sixteen selected captured-context
replay jobs compiled, and all 19 modules passed SPIRV-Tools v2026.1 validation
for Vulkan 1.2 (compute 12, fragment 3, geometry 2, vertex 2), including the three
later IMAD programs using real environments captured in the final cache.

The gameplay executable SHA-256 was
`4E03A46D307C67869AC41FD590A3D653DA16CBDC625400265F1AF3AF610BAD15`.
On RTX 5070 / NVIDIA 621.14 / Windows 11, it loaded preserved 11-star progress,
rendered habitat reentry, movement, jumping and creature pickup, and returned to
the book through End Exploration. A ten-second gameplay sample measured
58.82 FPS, mean 17.00 ms, p99 20.88 ms and no reported stutters. The earlier
candidate supported approximately 28 minutes of owner-controlled first-level
play after the previously black post-intro scene rendered. This is observed
first-level compatibility, not a complete-game or multi-GPU claim. Captures,
saves, logs and shader/cache data remain local.

## Initial standalone measurements

Source base: `ebf4b82d9b92288484021367187c36598b6db409` on `compat/yoshi-build150`.
Only the standalone target was added in the `tooling/ampere-quick-tests` worktree.
The first Yoshi package and its running CI job were left unchanged.

Existing Windows tools, with no toolchain installation:

- Visual Studio Build Tools 2022; MSVC compiler 19.44.35229 and linker 14.44.35229.0.
- Windows SDK 10.0.26100.0, x64 Visual Studio generator, MSBuild 17.14.60.
- CMake 3.31.6-msvc6. Ninja 1.12.1 is available but was not used for these runs.
- Four build jobs, Release configuration, no compiler-cache launcher.

This compiler differs from CI's clang-cl 22.1.8. These results establish a local MSVC check,
not matching compiler output or Vulkan/gameplay behavior.

| Case | Observed time | Result |
|---|---:|---|
| First configure, including the pinned Catch2 download | 6.0 s, reported by CMake | Pass |
| First prototype build | 69.75 s | Link failed: Catch2 used an older C++ standard than the tests |
| Reconfigure after setting C++20 before Catch2 | 1.19 s | Pass |
| First successful build after that correction | 77.20 s | Pass; reused test objects, so not a clean cold build |
| Unchanged `ampere-quick-tests` target rebuild | 0.57 s | Pass; no test sources compiled |
| CTest, stopwatch wall time | 0.22 s | 2/2 checks passed |
| Equivalent preprocessing-token change to a production header | 34.61 s | Three test sources rebuilt; 2/2 CTest checks passed |

The direct runner passed 29 test cases and 2,019 assertions. A deliberately absent tag returned
exit code 2 even with `--allow-running-no-tests`, and CTest verified the same contract.

The representative header change was `216 - high_uniforms` to `216U - high_uniforms` in
`lower_to_maxwell.h`: the expression remains unsigned but its preprocessing tokens change.
It was made only in the isolated worktree. The original header bytes were restored in a
`finally` block and checked with SHA-256; no production-header change is committed.

Commands were the Visual Studio sequence in [README.md](README.md), plus the direct runner
with `--reporter compact`. The first two builds used the default build target; the unchanged
and header-edit measurements explicitly used `--target ampere-quick-tests`.

Compiler-cache hit rates, Ninja build timings, a genuinely clean cold build and peak memory
were not measured here. They require separate measurements and must not be inferred from the
unchanged MSBuild time.
