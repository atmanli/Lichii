# Switch 2 Emulator Diagnostics MCP Server

A community-ready **Model Context Protocol (MCP)** server providing real-time telemetry, automated diagnostics, Vulkan rendering inspection, and deep **Shader Cache v2** analysis for Switch 2 emulators (**Eglantine**, **Lichii**, **Fritchii**, and **Eden**).

Allows AI coding assistants (**Claude Code**, **Cursor**, **Codex**, **Antigravity**) to inspect live gameplay, monitor pipeline compilation, detect bottlenecks, and capture viewport frames without manual log copying.

Conforms to standard **MCP Protocol 2024-11-05** over `stdio`.

---

## Features

- **Deep Shader Cache v2 Parser (PR #1036)**: Inspects `lichii-cache.v2.bin` and `lichii-cache.v2.driver.bin` directly using memory mapping (`mmap`). Reports graphics vs. compute pipelines, legacy v50–v54 migrations, tombstones, and dead bytes with CRC-32 verification and optional XXH3-64 checks.
- **Cross-Platform**: Works on Windows and Linux (including SteamOS / Steam Deck). Automatically discovers portable `user/` folders, global `%APPDATA%\eden`, or standard XDG paths (`~/.cache/eden`, `~/.local/share/eden`, `~/.config/eden`).
- **Zero Mandatory Dependencies**: Runs entirely on the Python 3.9+ standard library. Gracefully enhances with `psutil`, `pywin32`, `pillow`, or `xxhash` if installed.
- **Live Performance & Telemetry**: Reads FPS, frametime (ms), and CPU/RAM usage in real-time.
- **Vulkan Defect Tracking**: Identifies *zero-stutter* skipped draws, validation warnings, and shader stage compilation faults.
- **Automated Game Diagnosis**: Pinpoints whether a frame drop is caused by GPU compute overload, JIT CPU saturation, or background shader building.
- **Viewport Capture**: Exports live game frames to PNG for vision-capable AI models with strict path traversal prevention.

---

## Quick Start (CLI Mode)

You can run diagnostics directly from the command line:

```bash
# Run unit test suite
python tools/mcp/emulator_mcp_server.py --test

# Query live status
python tools/mcp/emulator_mcp_server.py --status

# Inspect Shader Cache v2 statistics
python tools/mcp/emulator_mcp_server.py --shaders

# Live performance & FPS
python tools/mcp/emulator_mcp_server.py --perf

# Run automated bottleneck diagnosis
python tools/mcp/emulator_mcp_server.py --diagnose
```

---

## AI Assistant Configuration

### 1. Claude Desktop & Claude Code

Add the following to `%APPDATA%\Claude\claude_desktop_config.json` (Windows):

```json
{
  "mcpServers": {
    "switch2-emulator": {
      "command": "python",
      "args": [
        "path/to/tools/mcp/emulator_mcp_server.py"
      ]
    }
  }
}
```

Or for Claude Code CLI:
```bash
claude mcp add switch2-emulator python tools/mcp/emulator_mcp_server.py
```

### 2. OpenAI Codex

In `~/.codex/config.toml` (Windows: `%USERPROFILE%\.codex\config.toml`):
```toml
[mcp_servers.switch2_emulator]
command = "python"
args = ["tools/mcp/emulator_mcp_server.py"]
```

Or register it directly via the Codex CLI:
```bash
codex mcp add switch2-emulator -- python tools/mcp/emulator_mcp_server.py
```

### 3. Cursor (`.cursor/mcp.json`)

```json
{
  "mcpServers": {
    "switch2-emulator": {
      "command": "python",
      "args": ["tools/mcp/emulator_mcp_server.py"]
    }
  }
}
```

### 4. Antigravity / Gemini

In your workspace or settings configuration:
```json
{
  "mcp": {
    "servers": {
      "emulator": {
        "command": "python",
        "args": ["tools/mcp/emulator_mcp_server.py"]
      }
    }
  }
}
```

---

## Available Tools

| Tool Name | Description |
|---|---|
| `emulator_get_status` | Returns emulator state, active Title ID, game name, uptime, and detected Vulkan physical device. |
| `emulator_get_performance` | Returns real-time FPS, frametime (ms), CPU load, and RAM usage. |
| `emulator_get_shader_stats` | Inspects `lichii-cache.v2.bin`, counts graphics/compute pipelines, migration stats, and dead bytes. |
| `emulator_get_rendering_defects` | Filters skipped draw events (zero-stutter bypass) and Vulkan validation errors. |
| `emulator_get_live_logs` | Extracts recent logs filtered by subsystem (`Render.Vulkan`, `Core.ARM`, etc.) and severity. |
| `emulator_diagnose_game` | Analyzes bottlenecks (CPU bound, GPU compute bound, shader stall) and provides recommendations. |
| `emulator_capture_screenshot` | Saves emulator window frame to PNG for multimodal visual inspection. |

*(Note: `eglantine_*` aliases are supported internally for backward compatibility).*

---

## Privacy & Security

- **Local Execution**: All introspection and log parsing occur strictly on the local machine.
- **Privacy Protection**: Viewport capture requires a confirmed, visible emulator window handle. Desktop-wide full screen captures are rejected.
- **Path Confinement**: Screenshots are constrained within a dedicated output directory and cannot escape through path traversal.
- **Telemetry Boundaries**: Log excerpts sent to AI assistants reflect emulator runtime state (subsystem errors, GPU device name). Avoid sharing logs containing personal directories if privacy is required.

---

## Optional Dependencies

```bash
pip install -r tools/mcp/requirements.txt
```
- `psutil` : Precise CPU core percentage and thread counts.
- `pillow` : Framebuffer PNG export for visual inspection.
- `pywin32` : Win32 window handle and coordinates acquisition on Windows.
- `xxhash` : Fast native XXH3-64 verification of shader record payloads.

---

## License

GPL-3.0-or-later. Compatible with Eden, Lichii, Fritchii, and Eglantine.
