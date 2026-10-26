"""Real authenticated servers sharing one discovery directory, without device IO."""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from PySide6.QtWidgets import QApplication

from comtoolx.api import ApiServer, discover_api, _live_discovery
from comtoolx.app import api_client
from comtoolx.controller import Controller
from comtoolx.plugins import register_builtins
from comtoolx.sdk import Registry

APP = QApplication.instance() or QApplication([])


class ApiDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.servers, self.controllers = [], []

    def tearDown(self):
        for server in reversed(self.servers):
            server.close()
        for controller in self.controllers:
            controller.close()
        self.temporary.cleanup()

    def start_window(self):
        """Run an actual HTTP service and controller while sharing one portable data folder."""
        registry = Registry()
        register_builtins(registry)
        controller = Controller(registry, self.root, self.root / "project")
        self.controllers.append(controller)
        server = ApiServer(controller)
        self.servers.append(server)
        server.start()
        return server

    def test_closing_newest_restores_the_authenticated_survivor(self):
        older, newer = self.start_window(), self.start_window()
        self.assertNotEqual(older.instance_path, newer.instance_path)
        self.assertEqual(newer.info, discover_api(self.root))
        newer.close()
        self.assertEqual(older.info, discover_api(self.root))
        self.assertTrue(_live_discovery(older.info))
        self.assertTrue(older.instance_path.exists())
        self.assertFalse(newer.instance_path.exists())

    def test_older_close_cannot_delete_a_newer_record_even_with_same_process_id(self):
        older, newer = self.start_window(), self.start_window()
        older.close()
        self.assertEqual(newer.info, json.loads(newer.info_path.read_text(encoding="utf-8")))
        self.assertEqual(newer.info, discover_api(self.root))
        newer.close()
        self.assertFalse(newer.info_path.exists())

    def test_missing_shared_entry_is_recovered_from_the_instance_catalogue(self):
        server = self.start_window()
        server.info_path.unlink()
        self.assertEqual(server.info, discover_api(self.root))
        self.assertEqual(server.info, json.loads(server.info_path.read_text(encoding="utf-8")))

    def test_window_repairs_both_deleted_records_and_partial_json(self):
        server = self.start_window()
        server.info_path.unlink()
        server.instance_path.unlink()
        server.repair_discovery()
        self.assertEqual(server.info, discover_api(self.root))
        self.assertEqual(server.info, json.loads(server.instance_path.read_text(encoding="utf-8")))
        server.info_path.write_text("{", encoding="utf-8")
        server.repair_discovery()
        self.assertEqual(server.info, discover_api(self.root))

    def test_repair_does_not_take_over_a_live_newer_window(self):
        older, newer = self.start_window(), self.start_window()
        older.repair_discovery()
        self.assertEqual(newer.info, discover_api(self.root))

    def test_dead_or_unauthenticated_alias_is_ignored(self):
        older, newer = self.start_window(), self.start_window()
        stale = dict(newer.info)
        newer.close()
        newer.instance_path.write_text(json.dumps(stale), encoding="utf-8")
        older.info_path.write_text(json.dumps(stale), encoding="utf-8")
        self.assertEqual(older.info, discover_api(self.root))
        wrong_token = {**older.info, "token": "invalid-test-token"}
        older.info_path.write_text(json.dumps(wrong_token), encoding="utf-8")
        self.assertEqual(older.info, discover_api(self.root))

    def test_cli_without_a_live_window_returns_an_actionable_json_error(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = api_client(["app.state", "--data-dir", str(self.root)])
        payload = json.loads(output.getvalue())
        self.assertEqual(1, result)
        self.assertFalse(payload["ok"])
        self.assertIn("未找到可连接", payload["error"])
        self.assertNotIn("Traceback", output.getvalue())


if __name__ == "__main__":
    unittest.main()
