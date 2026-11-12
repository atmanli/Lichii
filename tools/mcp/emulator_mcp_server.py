#!/usr/bin/env python3
"""
Switch 2 Emulator Diagnostics MCP Server
Compatible with Eglantine, Lichii, Fritchii, and Eden emulators.

Provides Model Context Protocol (MCP) tools for real-time telemetry,
performance analysis, Shader Cache v2 inspection (PR #1036), rendering
defects tracking, live log extraction, and automated bottleneck diagnosis.

Standard MCP Protocol Version: 2024-11-05 (JSON-RPC 2.0 over stdio)
"""

from __future__ import annotations

import sys
import os
import json
import time
import re
import mmap
import struct
import zlib
import subprocess
from pathlib import Path
from datetime import datetime, timezone
from typing import Optional, Dict, Any, List, Tuple

# Optional dependencies with graceful fallback
try:
    import psutil
except ImportError:
    psutil = None

try:
    import win32gui
    import win32process
    HAS_WIN32 = True
except ImportError:
    HAS_WIN32 = False

try:
    from PIL import Image, ImageGrab
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

try:
    import xxhash
    HAS_XXHASH = True
except ImportError:
    HAS_XXHASH = False


# ==============================================================================
# Path and Environment Discovery (Compliant with path_util.cpp / XDG)
# ==============================================================================

EMULATOR_NAMES = {'eglantine', 'lichii', 'fritchii', 'eden', 'eden-cli'}


def get_default_paths() -> Dict[str, Path]:
    """Computes default config, cache, and data paths according to OS standards."""
    home = Path.home()

    if os.name == 'nt':
        appdata_env = os.environ.get("APPDATA")
        appdata = Path(appdata_env) if appdata_env else (home / "AppData" / "Roaming")
        return {
            "config": appdata / "eden" / "config" / "qt-config.ini",
            "shader_root": appdata / "eden" / "cache" / "shader",
            "log": appdata / "eden" / "log" / "eden_log.txt",
            "telemetry": appdata / "eden" / "telemetry" / "live_state.json",
            "screenshots": appdata / "switch2_mcp" / "screenshots",
            "base": appdata / "eden"
        }
    else:
        xdg_config = Path(os.environ.get("XDG_CONFIG_HOME") or (home / ".config"))
        xdg_cache = Path(os.environ.get("XDG_CACHE_HOME") or (home / ".cache"))
        xdg_data = Path(os.environ.get("XDG_DATA_HOME") or (home / ".local" / "share"))

        return {
            "config": xdg_config / "eden" / "qt-config.ini",
            "shader_root": xdg_cache / "eden" / "shader",
            "log": xdg_data / "eden" / "log" / "eden_log.txt",
            "telemetry": xdg_data / "eden" / "telemetry" / "live_state.json",
            "screenshots": xdg_data / "switch2_mcp" / "screenshots",
            "base": xdg_config / "eden"
        }


def resolve_active_paths(target_dir: Optional[str] = None) -> Dict[str, Path]:
    """
    Resolves paths for config, logs, telemetry, and shader cache.
    Explicit target_dir takes absolute precedence, followed by running process,
    portable directory layout, and finally OS defaults.
    """
    if target_dir:
        t = Path(target_dir)
        user_dir = t / "user" if (t / "user").is_dir() else t
        return {
            "config": user_dir / "config" / "qt-config.ini",
            "shader_root": user_dir / "cache" / "shader",
            "log": user_dir / "log" / "eden_log.txt",
            "telemetry": user_dir / "telemetry" / "live_state.json",
            "screenshots": user_dir / "screenshots",
            "base": t
        }

    # Check running process for portable user/ layout
    proc = find_emulator_process()
    if proc:
        try:
            # Check process working directory first on Linux
            if psutil and hasattr(proc, 'cwd'):
                p_cwd = Path(proc.cwd())
                if (p_cwd / "user").is_dir():
                    user_dir = p_cwd / "user"
                    return {
                        "config": user_dir / "config" / "qt-config.ini",
                        "shader_root": user_dir / "cache" / "shader",
                        "log": user_dir / "log" / "eden_log.txt",
                        "telemetry": user_dir / "telemetry" / "live_state.json",
                        "screenshots": user_dir / "screenshots",
                        "base": p_cwd
                    }

            exe_path = getattr(proc, 'exe', None)
            if callable(exe_path):
                exe_dir = Path(exe_path()).parent
                if (exe_dir / "user").is_dir():
                    user_dir = exe_dir / "user"
                    return {
                        "config": user_dir / "config" / "qt-config.ini",
                        "shader_root": user_dir / "cache" / "shader",
                        "log": user_dir / "log" / "eden_log.txt",
                        "telemetry": user_dir / "telemetry" / "live_state.json",
                        "screenshots": user_dir / "screenshots",
                        "base": exe_dir
                    }
        except Exception:
            pass

    # Check environment variable overrides
    for env_var in ("EDEN_EMU_DIR", "LICHII_DIR", "EGLANTINE_DIR", "FRITCHII_DIR"):
        val = os.environ.get(env_var)
        if val:
            p = Path(val)
            user_dir = p / "user" if (p / "user").is_dir() else p
            return {
                "config": user_dir / "config" / "qt-config.ini",
                "shader_root": user_dir / "cache" / "shader",
                "log": user_dir / "log" / "eden_log.txt",
                "telemetry": user_dir / "telemetry" / "live_state.json",
                "screenshots": user_dir / "screenshots",
                "base": p
            }

    return get_default_paths()


# ==============================================================================
# Process and Window Introspection
# ==============================================================================

def find_emulator_process() -> Optional[Any]:
    """Locates any active emulator process using exact process name matching."""
    if psutil:
        try:
            for proc in psutil.process_iter(['pid', 'name', 'cmdline', 'create_time', 'memory_info']):
                pname = (proc.info.get('name') or '').lower()
                stem = Path(pname).stem
                if stem in EMULATOR_NAMES:
                    return proc
        except Exception:
            pass

    if os.name == 'nt':
        try:
            flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
            res = subprocess.run(
                ['tasklist', '/FO', 'CSV', '/NH'],
                capture_output=True, text=True, errors='ignore', timeout=3.0,
                creationflags=flags
            )
            for line in res.stdout.splitlines():
                parts = [p.strip(' "') for p in line.split('","')]
                if len(parts) >= 2:
                    stem = Path(parts[0]).stem.lower()
                    if stem in EMULATOR_NAMES:
                        try:
                            pid = int(parts[1])
                            return {"pid": pid, "name": parts[0]}
                        except ValueError:
                            pass
        except Exception:
            pass

    return None


def find_emulator_window(pid: Optional[int] = None) -> Tuple[Optional[int], str]:
    """Finds the main window handle and window title belonging to the emulator process."""
    if os.name == 'nt' and HAS_WIN32:
        result: List[Any] = [None, ""]

        def enum_cb(hwnd: int, _: Any) -> bool:
            try:
                if win32gui.IsWindowVisible(hwnd):
                    _, win_pid = win32process.GetWindowThreadProcessId(hwnd)
                    if pid and win_pid != pid:
                        return True
                    t = win32gui.GetWindowText(hwnd)
                    if t:
                        lower_t = t.lower()
                        if any(k in lower_t for k in EMULATOR_NAMES):
                            result[0] = hwnd
                            result[1] = t
                            return False
            except Exception:
                pass
            return True

        try:
            win32gui.EnumWindows(enum_cb, None)
        except Exception:
            pass

        if result[0]:
            return result[0], result[1]

    return None, ""


def read_head_bytes(path: Path, max_bytes: int = 64 * 1024) -> bytes:
    """Reads the first max_bytes from a file."""
    try:
        with open(path, "rb") as f:
            return f.read(max_bytes)
    except Exception:
        return b""


def read_tail_bytes(path: Path, max_bytes: int = 64 * 1024) -> bytes:
    """Reads the last max_bytes from a file without loading it all."""
    try:
        size = path.stat().st_size
        read_len = min(size, max_bytes)
        with open(path, "rb") as f:
            f.seek(size - read_len)
            return f.read(read_len)
    except Exception:
        return b""


def get_active_game_info(proc: Any = None, log_path: Optional[Path] = None) -> Dict[str, Any]:
    """Extracts Title ID and game name from process args, window title, or logs."""
    game_info = {
        "title_name": None,
        "title_id": None,
        "rom_path": None,
        "source": "unavailable"
    }

    # 1. From process command-line
    if proc and hasattr(proc, 'cmdline'):
        try:
            cmdline = proc.cmdline()
            for i, arg in enumerate(cmdline):
                if arg == '-g' and i + 1 < len(cmdline):
                    gpath = cmdline[i + 1]
                    game_info["rom_path"] = gpath
                    game_info["title_name"] = Path(gpath).stem
                    game_info["source"] = "process_commandline"
        except Exception:
            pass

    # 2. From log file (check both head and tail)
    if log_path and log_path.exists():
        try:
            head_text = read_head_bytes(log_path, 128 * 1024).decode('utf-8', errors='ignore')
            tail_text = read_tail_bytes(log_path, 64 * 1024).decode('utf-8', errors='ignore')
            combined_lines = head_text.splitlines() + tail_text.splitlines()

            for line in combined_lines:
                m = re.search(r'Booting game:\s*(?:0x)?([0-9a-fA-F]{16})\s*(?:\|\s*([^|\r\n]+))?', line)
                if m:
                    game_info["title_id"] = m.group(1).upper()
                    if m.group(2) and not game_info["title_name"]:
                        game_info["title_name"] = m.group(2).strip()
                    game_info["source"] = "eden_log"
                    break
                m_tid = re.search(r'Title ID:\s*(?:0x)?([0-9a-fA-F]{16})', line)
                if m_tid and not game_info["title_id"]:
                    game_info["title_id"] = m_tid.group(1).upper()
                    game_info["source"] = "eden_log"
        except Exception:
            pass

    # 3. From window title fallback
    if not game_info["title_name"]:
        pid = proc.get("pid") if isinstance(proc, dict) else getattr(proc, 'pid', None)
        _, win_title = find_emulator_window(pid)
        if win_title and '|' in win_title:
            parts = [p.strip() for p in win_title.split('|')]
            if len(parts) >= 2 and parts[1]:
                game_info["title_name"] = parts[1]
                game_info["source"] = "window_title"

    return game_info


def get_vulkan_device_info(log_path: Optional[Path] = None) -> Dict[str, Any]:
    """Extracts GPU vendor, device name, and Vulkan driver version from log startup."""
    gpu_info = {
        "backend": "Vulkan",
        "device_name": None,
        "driver_version": None
    }

    if log_path and log_path.exists():
        try:
            # Device and Driver are logged at Vulkan initialization (beginning of log)
            head_bytes = read_head_bytes(log_path, 128 * 1024)
            head_text = head_bytes.decode('utf-8', errors='ignore')
            for line in head_text.splitlines():
                if 'Render.Vulkan' in line or 'Vulkan' in line:
                    m_dev = re.search(r'Device:\s*(.+)', line)
                    if m_dev and not gpu_info["device_name"]:
                        gpu_info["device_name"] = m_dev.group(1).strip()
                    m_drv = re.search(r'Driver:\s*(.+)', line)
                    if m_drv and not gpu_info["driver_version"]:
                        gpu_info["driver_version"] = m_drv.group(1).strip()
                if gpu_info["device_name"] and gpu_info["driver_version"]:
                    break
        except Exception:
            pass

    return gpu_info


# ==============================================================================
# Deep Shader Cache v2 Parser (Aligned with cache_format.cpp @0ab7736df9)
# ==============================================================================

RECORD_KINDS = {
    1: "Program",
    2: "Graphics",
    3: "Compute",
    4: "Tombstone",
    5: "Used",
    6: "Migrated",
    7: "DeviceIdentity",
    8: "GraphicsBinary",
    9: "ComputeBinary",
}

RECORD_MAGIC = 0x5243584C  # "LXCR"
FILE_MAGIC = b"LXSHCACH"
FILE_HEADER_SIZE = 32
RECORD_HEADER_SIZE = 24
RECORD_HASH_SIZE = 8
RECORD_ALIGNMENT = 8
MAX_PAYLOAD_SIZE = 256 << 20  # 256 MiB


def _read_record_header(data: bytes, offset: int) -> Optional[Tuple[int, int, int, int]]:
    """Validates record magic, CRC-32, kind and max payload size."""
    if offset + RECORD_HEADER_SIZE > len(data):
        return None
    rmagic, kind, flags, payload_size, key, crc = struct.unpack_from("<IHHIQI", data, offset)
    if rmagic != RECORD_MAGIC:
        return None
    if crc != zlib.crc32(data[offset:offset + 20]):
        return None
    if kind == 0 or payload_size > MAX_PAYLOAD_SIZE:
        return None
    return kind, flags, payload_size, key


def _find_record(data: bytes, offset: int) -> int:
    """Finds the next 8-byte aligned offset holding an intact record header."""
    while offset + RECORD_HEADER_SIZE <= len(data):
        if _read_record_header(data, offset):
            return offset
        offset += RECORD_ALIGNMENT
    return len(data)


def parse_shader_cache_v2(cache_file_path: Path) -> Dict[str, Any]:
    """
    Parses a Shader Cache v2 log (lichii-cache.v2.bin or driver.bin) using mmap.
    Reproduces ParseLog from cache_format.cpp with CRC verification, tombstones,
    torn-tail detection, and payload hash checks.
    """
    path = Path(cache_file_path)
    if not path.exists():
        return {"status": "NOT_FOUND"}

    file_size = path.stat().st_size
    if file_size == 0:
        return {"header_status": "Empty", "file_size_bytes": 0}

    if file_size < FILE_HEADER_SIZE:
        return {
            "header_status": "Damaged",
            "file_size_bytes": file_size,
            "torn_tail_bytes": file_size,
            "dead_bytes": file_size
        }

    try:
        f = open(path, "rb")
        data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    except Exception as e:
        return {"status": "READ_ERROR", "error": str(e)}

    try:
        # Parse File Header (32 bytes)
        header_status = "Damaged"
        created_iso = None
        created_unix = 0

        if data[:8] == FILE_MAGIC and struct.unpack_from("<I", data, 24)[0] == zlib.crc32(data[:24]):
            version = struct.unpack_from("<I", data, 8)[0]
            if version != 2:
                return {
                    "header_status": "OtherVersion",
                    "version": version,
                    "file_size_bytes": file_size
                }
            hdr_size = struct.unpack_from("<I", data, 12)[0]
            header_status = "Valid" if hdr_size == FILE_HEADER_SIZE else "Damaged"
            created_unix = struct.unpack_from("<Q", data, 16)[0]
            try:
                created_iso = datetime.fromtimestamp(created_unix, tz=timezone.utc).isoformat()
            except Exception:
                created_iso = "InvalidTimestamp"

        stats: Dict[str, Any] = {
            "header_status": header_status,
            "format": "v2 (PR #1036)",
            "file_size_bytes": file_size,
            "file_size_mb": round(file_size / (1024 * 1024), 2),
            "created_date_utc": created_iso,
            "records": 0,
            "live": 0,
            "superseded": 0,
            "tombstones": 0,
            "bad_headers": 0,
            "bad_payloads": 0,
            "skipped_bytes": 0,
            "torn_tail_bytes": 0,
            "dead_bytes": 0,
            "payload_hashes": "verified" if HAS_XXHASH else "unverified (xxhash optional)",
            "breakdown": {
                "graphics_pipelines": 0,
                "compute_pipelines": 0,
                "programs": 0,
                "migrated_legacy_records": 0,
                "used_entries": 0,
                "driver_binaries": 0
            }
        }

        # Track latest records: (kind, key) -> entry_index
        entries: List[Dict[str, Any]] = []
        latest: Dict[Tuple[int, int], int] = {}

        offset = FILE_HEADER_SIZE
        while offset < len(data):
            left = len(data) - offset
            if left < RECORD_HEADER_SIZE:
                stats["torn_tail_bytes"] = left
                break

            header = _read_record_header(data, offset)
            if not header:
                stats["bad_headers"] += 1
                next_off = _find_record(data, offset + RECORD_ALIGNMENT)
                stats["skipped_bytes"] += next_off - offset
                offset = next_off
                continue

            kind, flags, payload_size, key = header
            framed = RECORD_HEADER_SIZE + payload_size + RECORD_HASH_SIZE

            if framed > left:
                next_off = _find_record(data, offset + RECORD_ALIGNMENT)
                if next_off == len(data):
                    stats["torn_tail_bytes"] = left
                    break
                stats["bad_payloads"] += 1
                stats["skipped_bytes"] += next_off - offset
                offset = next_off
                continue

            payload = data[offset + RECORD_HEADER_SIZE:offset + RECORD_HEADER_SIZE + payload_size]

            # Verify payload hash if xxhash is installed
            if HAS_XXHASH:
                expected_hash = struct.unpack_from("<Q", data, offset + RECORD_HEADER_SIZE + payload_size)[0]
                calc_hash = xxhash.xxh3_64_intdigest(payload, seed=key ^ (kind << 48))
                if expected_hash != calc_hash:
                    stats["bad_payloads"] += 1
                    next_off = _find_record(data, offset + RECORD_ALIGNMENT)
                    stats["skipped_bytes"] += next_off - offset
                    offset = next_off
                    continue

            raw_size = RECORD_HEADER_SIZE + payload_size + RECORD_HASH_SIZE
            stride = (raw_size + RECORD_ALIGNMENT - 1) // RECORD_ALIGNMENT * RECORD_ALIGNMENT
            actual_size = min(stride, left)

            stats["records"] += 1
            kind_key = (kind, key)

            if kind == 4:  # Tombstone
                stats["tombstones"] += 1
                stats["dead_bytes"] += actual_size
                # cache_format.cpp line 214: payload size must be strictly sizeof(u16) == 2
                if len(payload) == 2:
                    target_kind = struct.unpack_from("<H", payload, 0)[0]
                    target_key = (target_kind, key)
                    if target_key in latest:
                        entries[latest[target_key]]["live"] = False
                        del latest[target_key]
            else:
                if kind_key in latest:
                    prev_idx = latest[kind_key]
                    entries[prev_idx]["live"] = False
                    del latest[kind_key]

                latest[kind_key] = len(entries)
                entries.append({
                    "kind": kind,
                    "key": key,
                    "size": actual_size,
                    "live": True
                })

            offset += actual_size

        # Calculate dead bytes and live breakdowns
        live_count = 0
        for entry in entries:
            if entry["live"]:
                live_count += 1
                k = entry["kind"]
                if k == 2:
                    stats["breakdown"]["graphics_pipelines"] += 1
                elif k == 3:
                    stats["breakdown"]["compute_pipelines"] += 1
                elif k == 1:
                    stats["breakdown"]["programs"] += 1
                elif k == 6:
                    stats["breakdown"]["migrated_legacy_records"] += 1
                elif k == 5:
                    stats["breakdown"]["used_entries"] += 1
                elif k in (8, 9):
                    stats["breakdown"]["driver_binaries"] += 1
            else:
                stats["superseded"] += 1
                stats["dead_bytes"] += entry["size"]

        stats["live"] = live_count
        stats["dead_bytes"] += stats["skipped_bytes"] + stats["torn_tail_bytes"]

        return stats
    finally:
        data.close()
        f.close()


# ==============================================================================
# Tool Implementations
# ==============================================================================

def tool_get_status(emulator_dir: Optional[str] = None) -> Dict[str, Any]:
    """Returns general emulator status, active game, GPU backend, and runtime metrics."""
    paths = resolve_active_paths(emulator_dir)
    proc = find_emulator_process()

    if not proc:
        return {
            "status": "STOPPED",
            "message": "Emulator process is not running.",
            "discovered_paths": {
                "base_dir": str(paths["base"]),
                "config_path": str(paths["config"]),
                "log_path": str(paths["log"]),
                "shader_root": str(paths["shader_root"])
            }
        }

    pid = proc.get("pid") if isinstance(proc, dict) else getattr(proc, 'pid', None)
    uptime_sec = None
    cpu_percent = None
    ram_mb = None
    threads = None

    if psutil and not isinstance(proc, dict):
        try:
            uptime_sec = time.time() - proc.create_time()
            raw_cpu = proc.cpu_percent(interval=0.02)
            cpu_count = psutil.cpu_count(logical=True) or 1
            cpu_percent = raw_cpu / cpu_count
            ram_mb = proc.memory_info().rss / (1024 * 1024)
            threads = proc.num_threads()
        except Exception:
            pass

    _, win_title = find_emulator_window(pid)
    game_info = get_active_game_info(proc, paths["log"])
    gpu_info = get_vulkan_device_info(paths["log"])

    return {
        "status": "RUNNING",
        "pid": pid,
        "uptime_formatted": f"{int(uptime_sec // 60)}m {int(uptime_sec % 60)}s" if uptime_sec else None,
        "cpu_usage_percent": round(cpu_percent, 1) if cpu_percent is not None else None,
        "ram_usage_mb": round(ram_mb, 1) if ram_mb is not None else None,
        "thread_count": threads,
        "window_title": win_title or None,
        "game": game_info,
        "graphics": {
            "backend": gpu_info["backend"],
            "device": gpu_info["device_name"],
            "driver_version": gpu_info["driver_version"]
        }
    }


def tool_get_performance(sample_duration_sec: float = 1.0, emulator_dir: Optional[str] = None) -> Dict[str, Any]:
    """Measures live FPS, frametime, and CPU/memory footprint."""
    sample_sec = max(0.1, min(float(sample_duration_sec), 5.0))
    paths = resolve_active_paths(emulator_dir)
    proc = find_emulator_process()

    if not proc:
        return {"error": "Emulator is not currently running."}

    # 1. Telemetry from live_state.json if present
    if paths["telemetry"].exists():
        try:
            with open(paths["telemetry"], 'r', encoding='utf-8') as f:
                data = json.load(f)
                if time.time() - data.get("timestamp", 0) < 3.0:
                    return {
                        "source": "live_state.json",
                        "fps": data.get("fps"),
                        "frametime_ms": data.get("frametime_ms"),
                        "frame_drops": data.get("frame_drops", 0)
                    }
        except Exception:
            pass

    # 2. Window title telemetry fallback
    pid = proc.get("pid") if isinstance(proc, dict) else getattr(proc, 'pid', None)
    _, win_title = find_emulator_window(pid)

    fps = None
    frametime_ms = None
    if win_title:
        m_fps = re.search(r'([0-9.]+)\s*FPS', win_title, re.IGNORECASE)
        if m_fps:
            fps = float(m_fps.group(1))
        m_ft = re.search(r'([0-9.]+)\s*ms', win_title, re.IGNORECASE)
        if m_ft:
            frametime_ms = float(m_ft.group(1))

    cpu_load = None
    mem_mb = None
    if psutil and not isinstance(proc, dict):
        try:
            raw_cpu = proc.cpu_percent(interval=sample_sec)
            cpu_count = psutil.cpu_count(logical=True) or 1
            cpu_load = raw_cpu / cpu_count
            mem_mb = proc.memory_info().rss / (1024 * 1024)
        except Exception:
            pass

    return {
        "source": "window_telemetry" if fps else "unavailable",
        "fps": round(fps, 2) if fps is not None else None,
        "frametime_ms": round(frametime_ms, 2) if frametime_ms is not None else None,
        "cpu_load_percent": round(cpu_load, 1) if cpu_load is not None else None,
        "ram_usage_mb": round(mem_mb, 1) if mem_mb is not None else None
    }


def tool_get_shader_stats(emulator_dir: Optional[str] = None) -> Dict[str, Any]:
    """
    Inspects Shader Cache v2 (lichii-cache.v2.bin and driver.bin).
    Reports compiled graphics & compute pipelines, migration from v54, and cache health.
    """
    paths = resolve_active_paths(emulator_dir)
    shader_root = paths["shader_root"]

    proc = find_emulator_process()
    active_game = get_active_game_info(proc, paths["log"])

    cached_titles = []
    total_cache_size_mb = 0.0

    if shader_root and shader_root.exists():
        try:
            for title_dir in shader_root.iterdir():
                if not title_dir.is_dir():
                    continue

                title_id = title_dir.name.upper()
                v2_file = title_dir / "lichii-cache.v2.bin"
                v2_driver_file = title_dir / "lichii-cache.v2.driver.bin"
                legacy_files = list(title_dir.glob("vulkan*.bin"))

                try:
                    title_size_mb = sum(f.stat().st_size for f in title_dir.glob("*.bin")) / (1024 * 1024)
                except Exception:
                    title_size_mb = 0.0
                total_cache_size_mb += title_size_mb

                entry: Dict[str, Any] = {
                    "title_id": title_id,
                    "total_size_mb": round(title_size_mb, 2),
                    "has_shader_cache_v2": v2_file.exists(),
                    "has_driver_binary_blob": v2_driver_file.exists(),
                    "legacy_v1_files_count": len(legacy_files)
                }

                if v2_file.exists():
                    entry["v2_details"] = parse_shader_cache_v2(v2_file)

                if v2_driver_file.exists():
                    entry["driver_blob_details"] = parse_shader_cache_v2(v2_driver_file)

                cached_titles.append(entry)
        except Exception as e:
            return {"error": f"Failed enumerating shader cache directory: {e}"}

    return {
        "active_game_title_id": active_game["title_id"],
        "total_shader_cache_size_mb": round(total_cache_size_mb, 2),
        "cached_titles_count": len(cached_titles),
        "cached_titles": cached_titles
    }


def tool_get_rendering_defects(tail_events: int = 30, emulator_dir: Optional[str] = None) -> Dict[str, Any]:
    """Extracts zero-stutter skipped draws and Vulkan validation errors from logs."""
    max_events = max(1, min(int(tail_events), 200))
    paths = resolve_active_paths(emulator_dir)
    log_path = paths["log"]

    skipped_draws = []
    vulkan_errors = []

    if log_path and log_path.exists():
        try:
            tail_bytes = read_tail_bytes(log_path, 256 * 1024)
            tail_text = tail_bytes.decode('utf-8', errors='ignore')
            for line in reversed(tail_text.splitlines()):
                line_str = line.strip()
                if not line_str:
                    continue
                if 'skip' in line_str.lower() and 'draw' in line_str.lower():
                    if len(skipped_draws) < max_events:
                        skipped_draws.append(line_str)
                if any(k in line_str for k in ('VK_ERROR', 'Validation Error', 'DeviceLost')):
                    if len(vulkan_errors) < max_events:
                        vulkan_errors.append(line_str)
                if len(skipped_draws) >= max_events and len(vulkan_errors) >= max_events:
                    break
        except Exception as e:
            return {"error": f"Log reading error: {e}"}

    return {
        "skipped_draws_count": len(skipped_draws),
        "vulkan_errors_count": len(vulkan_errors),
        "recent_skipped_draws": skipped_draws,
        "recent_vulkan_errors": vulkan_errors
    }


def tool_get_live_logs(
    subsystem: str = "all",
    severity: str = "Warning",
    max_lines: int = 50,
    emulator_dir: Optional[str] = None
) -> Dict[str, Any]:
    """Filters recent logs by subsystem and severity without loading entire files into memory."""
    try:
        limit = max(1, min(int(max_lines), 500))
    except (ValueError, TypeError):
        return {"error": "max_lines must be an integer"}

    severity_levels = {
        "trace": 1,
        "debug": 2,
        "info": 3,
        "warning": 4,
        "error": 5,
        "critical": 6
    }
    target_lvl = severity_levels.get(str(severity).lower(), 4)

    paths = resolve_active_paths(emulator_dir)
    log_path = paths["log"]

    if not log_path or not log_path.exists():
        return {"error": "Log file not found", "path": str(log_path)}

    events = []
    try:
        tail_bytes = read_tail_bytes(log_path, 512 * 1024)
        tail_text = tail_bytes.decode('utf-8', errors='ignore')
        for line in reversed(tail_text.splitlines()):
            if len(events) >= limit:
                break
            line_str = line.strip()
            if not line_str.startswith('['):
                continue

            m = re.match(r'\[\s*([0-9.]+)\]\s*([A-Za-z0-9_.]+)\s*<([A-Za-z]+)>\s*(.*)', line_str)
            if m:
                ts, sub, sev, msg = m.groups()
                sev_val = severity_levels.get(sev.lower(), 3)
                if sev_val < target_lvl:
                    continue
                if subsystem.lower() != "all" and subsystem.lower() not in sub.lower():
                    continue
                events.append({
                    "timestamp": float(ts),
                    "subsystem": sub,
                    "severity": sev,
                    "message": msg
                })
    except Exception as e:
        return {"error": str(e)}

    return {"count": len(events), "events": events}


def tool_diagnose_game(emulator_dir: Optional[str] = None) -> Dict[str, Any]:
    """Performs automated performance and stability bottleneck diagnosis."""
    paths = resolve_active_paths(emulator_dir)
    proc = find_emulator_process()

    if not proc:
        return {"status": "STOPPED", "message": "Emulator is not running."}

    perf = tool_get_performance(sample_duration_sec=0.2, emulator_dir=emulator_dir)
    defects = tool_get_rendering_defects(tail_events=10, emulator_dir=emulator_dir)

    fps = perf.get("fps")
    frametime = perf.get("frametime_ms")
    cpu_load = perf.get("cpu_load_percent")

    bottleneck = "UNDETERMINED"
    recommendations = []

    if fps is not None:
        if fps < 30.0:
            if cpu_load and cpu_load > 85.0:
                bottleneck = "CPU_BOUND (JIT translation or dispatch thread saturation)"
                recommendations.append("Reduce CPU accuracy or check host thread scheduling.")
            else:
                bottleneck = "GPU_OR_SHADER_BOUND (Heavy pipeline generation or high resolution)"
                recommendations.append("Verify Shader Cache v2 hit rate or lower resolution scale.")
        else:
            bottleneck = "OPTIMAL (Target performance achieved)"
    else:
        bottleneck = "UNKNOWN (Live FPS unavailable from viewport)"

    return {
        "active_bottleneck": bottleneck,
        "fps": fps,
        "frametime_ms": frametime,
        "cpu_usage_pct": cpu_load,
        "vulkan_errors_detected": defects.get("vulkan_errors_count", 0),
        "recommendations": recommendations
    }


def tool_capture_screenshot(save_name: str = "emulator_frame.png", emulator_dir: Optional[str] = None) -> Dict[str, Any]:
    """Captures screenshot of the emulator viewport strictly within the application window."""
    # Sanitize save_name against path traversal
    if not re.match(r"^[\w.-]{1,64}\.png$", save_name):
        return {"error": "Invalid save_name. Must be a filename ending in .png without path separators."}

    proc = find_emulator_process()
    if not proc:
        return {"error": "Emulator is not running."}

    pid = proc.get("pid") if isinstance(proc, dict) else getattr(proc, 'pid', None)
    hwnd, title = find_emulator_window(pid)

    if not hwnd or hwnd == 0:
        return {"error": "Emulator window not located; full desktop capture refused for privacy."}

    paths = resolve_active_paths(emulator_dir)
    out_dir = paths["screenshots"]
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = (out_dir / save_name).resolve()

    if not out_path.is_relative_to(out_dir.resolve()):
        return {"error": "Path traversal detected."}

    if os.name == 'nt' and HAS_PIL and HAS_WIN32:
        try:
            rect = win32gui.GetWindowRect(hwnd)
            img = ImageGrab.grab(bbox=rect)
            img.save(str(out_path))
            return {
                "status": "SUCCESS",
                "saved_path": str(out_path),
                "dimensions": f"{img.width}x{img.height}",
                "window_title": title
            }
        except Exception as e:
            return {"error": f"Capture error: {e}"}

    return {"error": "Viewport capture requires Pillow and pywin32 on Windows."}


# ==============================================================================
# MCP Protocol Handlers & Registry
# ==============================================================================

TOOL_DEFINITIONS = [
    {
        "name": "emulator_get_status",
        "description": "Get current emulator runtime status, active title, uptime, CPU/RAM, and Vulkan GPU backend.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "emulator_dir": {"type": "string", "description": "Optional path to emulator base directory."}
            },
            "additionalProperties": False
        },
        "handler": tool_get_status
    },
    {
        "name": "emulator_get_performance",
        "description": "Measure real-time FPS, frametimes (ms), and CPU utilization.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "sample_duration_sec": {"type": "number", "description": "Duration to sample CPU load.", "default": 1.0, "minimum": 0.1, "maximum": 5.0},
                "emulator_dir": {"type": "string", "description": "Optional path to emulator base directory."}
            },
            "additionalProperties": False
        },
        "handler": tool_get_performance
    },
    {
        "name": "emulator_get_shader_stats",
        "description": "Inspect Shader Cache v2 (lichii-cache.v2.bin) records, compute/graphics pipelines, and legacy migration.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "emulator_dir": {"type": "string", "description": "Optional path to emulator base directory."}
            },
            "additionalProperties": False
        },
        "handler": tool_get_shader_stats
    },
    {
        "name": "emulator_get_rendering_defects",
        "description": "Inspect skipped draws (zero-stutter indicator) and Vulkan validation or compilation errors.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "tail_events": {"type": "integer", "description": "Number of recent events to return.", "default": 30, "minimum": 1, "maximum": 200},
                "emulator_dir": {"type": "string", "description": "Optional path to emulator base directory."}
            },
            "additionalProperties": False
        },
        "handler": tool_get_rendering_defects
    },
    {
        "name": "emulator_get_live_logs",
        "description": "Filter and extract emulator logs by subsystem (e.g. Render.Vulkan, Core.ARM) and severity.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "subsystem": {"type": "string", "description": "Log subsystem filter or 'all'.", "default": "all"},
                "severity": {"type": "string", "description": "Severity level.", "enum": ["Trace", "Debug", "Info", "Warning", "Error", "Critical"], "default": "Warning"},
                "max_lines": {"type": "integer", "description": "Max lines to return.", "default": 50, "minimum": 1, "maximum": 500},
                "emulator_dir": {"type": "string", "description": "Optional path to emulator base directory."}
            },
            "additionalProperties": False
        },
        "handler": tool_get_live_logs
    },
    {
        "name": "emulator_diagnose_game",
        "description": "Perform comprehensive automated diagnosis of bottlenecks (CPU bound, GPU compute, Shader stall).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "emulator_dir": {"type": "string", "description": "Optional path to emulator base directory."}
            },
            "additionalProperties": False
        },
        "handler": tool_diagnose_game
    },
    {
        "name": "emulator_capture_screenshot",
        "description": "Capture screenshot of the emulator viewport for multimodal AI visual inspection.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "save_name": {"type": "string", "description": "Filename ending in .png.", "default": "emulator_frame.png"},
                "emulator_dir": {"type": "string", "description": "Optional path to emulator base directory."}
            },
            "additionalProperties": False
        },
        "handler": tool_capture_screenshot
    }
]

# Register handlers and backward aliases
REGISTRY: Dict[str, Any] = {}
SCHEMAS: Dict[str, Any] = {}
for tool in TOOL_DEFINITIONS:
    REGISTRY[tool["name"]] = tool["handler"]
    SCHEMAS[tool["name"]] = tool["inputSchema"]
    # Register eglantine alias internally for backward compat
    alias = tool["name"].replace("emulator_", "eglantine_")
    REGISTRY[alias] = tool["handler"]
    SCHEMAS[alias] = tool["inputSchema"]


def _err(msg_id: Any, code: int, message: str) -> Dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": msg_id,
        "error": {
            "code": code,
            "message": message
        }
    }


def _ok(msg_id: Any, result: Any) -> Dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": msg_id,
        "result": result
    }


def handle_json_rpc(line: str) -> Optional[Dict[str, Any]]:
    """Robust JSON-RPC 2.0 processor conforming to MCP 2024-11-05."""
    try:
        req = json.loads(line)
    except Exception:
        return _err(None, -32700, "Parse error")

    if not isinstance(req, dict):
        return _err(None, -32600, "Invalid Request: root must be an object")

    if req.get("jsonrpc") != "2.0" or not isinstance(req.get("method"), str):
        return _err(req.get("id"), -32600, "Invalid Request: missing jsonrpc 2.0 or method")

    msg_id = req.get("id")
    is_notification = "id" not in req
    method = req["method"]
    params = req.get("params")

    if params is not None and not isinstance(params, dict):
        return None if is_notification else _err(msg_id, -32602, "params must be an object")

    params_dict = params or {}

    # Never respond to notifications
    if is_notification or method.startswith("notifications/"):
        return None

    if method == "initialize":
        return _ok(msg_id, {
            "protocolVersion": "2024-11-05",
            "capabilities": {
                "tools": {}
            },
            "serverInfo": {
                "name": "nx2-emulator-diagnostics",
                "version": "2.2.0"
            }
        })
    elif method == "ping":
        return _ok(msg_id, {})
    elif method == "tools/list":
        # Advertise only canonical 7 tools to save token context
        tools_list = [
            {
                "name": t["name"],
                "description": t["description"],
                "inputSchema": t["inputSchema"]
            }
            for t in TOOL_DEFINITIONS
        ]
        return _ok(msg_id, {"tools": tools_list})
    elif method == "tools/call":
        tool_name = params_dict.get("name")
        if not tool_name or tool_name not in REGISTRY:
            return _err(msg_id, -32602, f"Unknown tool: '{tool_name}'")

        raw_args = params_dict.get("arguments")
        if raw_args is not None and not isinstance(raw_args, dict):
            return _err(msg_id, -32602, "arguments must be an object")

        args = raw_args or {}
        fn = REGISTRY[tool_name]

        try:
            res = fn(**args)
            is_error = isinstance(res, dict) and "error" in res
            return _ok(msg_id, {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(res, indent=2, ensure_ascii=False)
                    }
                ],
                "isError": is_error
            })
        except TypeError as te:
            return _err(msg_id, -32602, f"Invalid tool arguments: {te}")
        except Exception as e:
            return _ok(msg_id, {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps({"error": str(e)})
                    }
                ],
                "isError": True
            })

    return _err(msg_id, -32601, f"Method '{method}' not found")


# ==============================================================================
# CLI Entrypoint
# ==============================================================================

def main() -> None:
    # Ensure UTF-8 console streams
    if hasattr(sys.stdin, "reconfigure"):
        sys.stdin.reconfigure(encoding="utf-8")
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    if len(sys.argv) > 1:
        arg = sys.argv[1].lower().lstrip('-')
        if arg in ('test', 'self-test'):
            import unittest
            tests_dir = Path(__file__).resolve().parent / "tests"
            suite = unittest.defaultTestLoader.discover(str(tests_dir))
            runner = unittest.TextTestRunner(verbosity=2)
            res = runner.run(suite)
            sys.exit(0 if res.wasSuccessful() else 1)
        elif arg in ('status', 'st'):
            res = tool_get_status()
        elif arg in ('perf', 'performance', 'fps'):
            res = tool_get_performance()
        elif arg in ('shaders', 'shader'):
            res = tool_get_shader_stats()
        elif arg in ('defects', 'defect'):
            res = tool_get_rendering_defects()
        elif arg in ('logs', 'log'):
            res = tool_get_live_logs()
        elif arg in ('diagnose', 'diag'):
            res = tool_diagnose_game()
        elif arg in ('screenshot', 'snap'):
            res = tool_capture_screenshot()
        else:
            res = {
                "usage": "python emulator_mcp_server.py [OPTION]",
                "options": ["--test", "--status", "--perf", "--shaders", "--defects", "--logs", "--diagnose", "--screenshot"]
            }
        print(json.dumps(res, indent=2, ensure_ascii=False))
        return

    # Standard MCP JSON-RPC stdio loop
    for line in sys.stdin:
        line_str = line.strip()
        if not line_str:
            continue
        try:
            resp = handle_json_rpc(line_str)
            if resp is not None:
                sys.stdout.write(json.dumps(resp) + "\n")
                sys.stdout.flush()
        except Exception as e:
            err_resp = _err(None, -32603, f"Internal JSON-RPC error: {e}")
            sys.stdout.write(json.dumps(err_resp) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
