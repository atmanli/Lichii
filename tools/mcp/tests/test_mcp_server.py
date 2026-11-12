#!/usr/bin/env python3
"""
Unit tests for Switch 2 Emulator Diagnostics MCP Server
Validates JSON-RPC message handling, tool execution, robustness to malformed inputs,
and Shader Cache v2 binary parsing (including CRC-32, tombstones, and XXH3 payload verification).
"""

import sys
import os
import json
import struct
import zlib
import tempfile
import unittest
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import emulator_mcp_server as mcp


def make_header(version=2, created=1700000000, valid_crc=True):
    h = bytearray(32)
    h[0:8] = mcp.FILE_MAGIC
    struct.pack_into("<IIQ", h, 8, version, 32, created)
    struct.pack_into("<I", h, 24, zlib.crc32(bytes(h[:24])) if valid_crc else 0)
    return bytes(h)


def make_record(kind, key, payload_bytes, pad=True, compute_hash=True):
    h = bytearray(24)
    struct.pack_into("<IHHIQ", h, 0, mcp.RECORD_MAGIC, kind, 0, len(payload_bytes), key)
    struct.pack_into("<I", h, 20, zlib.crc32(bytes(h[:20])))

    # Compute genuine XXH3-64 hash if xxhash is present
    if compute_hash and mcp.HAS_XXHASH:
        p_hash_int = mcp.xxhash.xxh3_64_intdigest(payload_bytes, seed=key ^ (kind << 48))
        p_hash = struct.pack("<Q", p_hash_int)
    else:
        p_hash = b"\x00" * 8

    b = bytes(h) + payload_bytes + p_hash
    if pad and len(b) % 8 != 0:
        b += b"\x00" * (8 - (len(b) % 8))
    return b


class TestMCPServer(unittest.TestCase):

    def test_json_rpc_initialize(self):
        req = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        resp = mcp.handle_json_rpc(req)
        self.assertIsNotNone(resp)
        self.assertEqual(resp["id"], 1)
        self.assertEqual(resp["result"]["protocolVersion"], "2024-11-05")
        self.assertEqual(resp["result"]["serverInfo"]["name"], "nx2-emulator-diagnostics")

    def test_json_rpc_tools_list_advertises_canonical_seven(self):
        req = json.dumps({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
        resp = mcp.handle_json_rpc(req)
        self.assertIsNotNone(resp)
        tools = resp["result"]["tools"]
        self.assertEqual(len(tools), 7)
        tool_names = [t["name"] for t in tools]
        self.assertIn("emulator_get_status", tool_names)
        self.assertIn("emulator_get_shader_stats", tool_names)
        # eglantine_* alias should not pollute tools/list
        self.assertNotIn("eglantine_get_status", tool_names)

    def test_json_rpc_error_handling(self):
        # Invalid JSON
        r1 = mcp.handle_json_rpc("{invalid json")
        self.assertEqual(r1["error"]["code"], -32700)

        # Batch request or root not object
        r2 = mcp.handle_json_rpc(json.dumps([{"jsonrpc": "2.0", "id": 1, "method": "ping"}]))
        self.assertEqual(r2["error"]["code"], -32600)

        # Notification should receive no response
        r3 = mcp.handle_json_rpc(json.dumps({"jsonrpc": "2.0", "method": "notifications/test"}))
        self.assertIsNone(r3)

        # Unknown tool should return -32602
        r4 = mcp.handle_json_rpc(json.dumps({
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/call",
            "params": {"name": "non_existent_tool"}
        }))
        self.assertEqual(r4["error"]["code"], -32602)

        # Non-object params
        r5 = mcp.handle_json_rpc(json.dumps({
            "jsonrpc": "2.0",
            "id": 5,
            "method": "tools/call",
            "params": "invalid_string"
        }))
        self.assertEqual(r5["error"]["code"], -32602)

    def test_tools_execution_safe(self):
        status = mcp.tool_get_status()
        self.assertIsInstance(status, dict)

        perf = mcp.tool_get_performance()
        self.assertIsInstance(perf, dict)

        shaders = mcp.tool_get_shader_stats()
        self.assertIsInstance(shaders, dict)

        defects = mcp.tool_get_rendering_defects()
        self.assertIsInstance(defects, dict)

        logs = mcp.tool_get_live_logs(max_lines=10)
        self.assertIsInstance(logs, dict)

        diag = mcp.tool_diagnose_game()
        self.assertIsInstance(diag, dict)

    def test_shader_cache_v2_tombstone_and_crc(self):
        with tempfile.NamedTemporaryFile(suffix=".v2.bin", delete=False) as f:
            temp_path = Path(f.name)
            h = make_header()
            # Record 1: Graphics pipeline
            g = make_record(2, 0x1234, b"SPIRV_GRAPHICS")
            # Record 2: Tombstone strictly 2 bytes removing record 1
            t = make_record(4, 0x1234, struct.pack("<H", 2))
            # Record 3: Compute pipeline
            c = make_record(3, 0x5678, b"SPIRV_COMPUTE")
            f.write(h + g + t + c)

        try:
            stats = mcp.parse_shader_cache_v2(temp_path)
            self.assertEqual(stats["header_status"], "Valid")
            self.assertEqual(stats["records"], 3)
            self.assertEqual(stats["tombstones"], 1)
            self.assertEqual(stats["live"], 1)  # Only compute pipeline is alive
            self.assertEqual(stats["breakdown"]["graphics_pipelines"], 0)
            self.assertEqual(stats["breakdown"]["compute_pipelines"], 1)
        finally:
            if temp_path.exists():
                temp_path.unlink()

    def test_shader_cache_v2_torn_tail(self):
        with tempfile.NamedTemporaryFile(suffix=".v2.bin", delete=False) as f:
            temp_path = Path(f.name)
            h = make_header()
            g = make_record(2, 0x1111, b"VALID_RECORD")
            # Truncated record in tail
            g_torn = make_record(2, 0x2222, b"LONG_RECORD" * 100)[:40]
            f.write(h + g + g_torn)

        try:
            stats = mcp.parse_shader_cache_v2(temp_path)
            self.assertEqual(stats["records"], 1)
            self.assertEqual(stats["live"], 1)
            self.assertEqual(stats["torn_tail_bytes"], 40)
            self.assertGreaterEqual(stats["dead_bytes"], 40)
        finally:
            if temp_path.exists():
                temp_path.unlink()

    def test_screenshot_security_path_traversal(self):
        r = mcp.tool_capture_screenshot(save_name="../../evil.png")
        self.assertIn("error", r)
        self.assertIn("Invalid save_name", r["error"])


if __name__ == "__main__":
    unittest.main()
