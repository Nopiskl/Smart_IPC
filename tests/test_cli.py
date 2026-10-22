#!/usr/bin/env python3
"""验证 SmartIPC 参数边界、密码输入、信号中断及终端回显恢复。"""

import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import tempfile
import termios
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build/host/apps/SmartIPC/SmartIPC"


class CLITests(unittest.TestCase):
    def setUp(self):
        if not BINARY.is_file():
            self.fail("Build first: ./build.sh --host")
        self.temporary = tempfile.TemporaryDirectory(prefix="nopiskl-cli-")
        self.data = Path(self.temporary.name) / "data"
        self.addCleanup(self.temporary.cleanup)

    def run_command(self, *arguments, password=None):
        return subprocess.run(
            [str(BINARY), "--data-dir", str(self.data), *arguments],
            input=password,
            text=True,
            capture_output=True,
            timeout=5,
        )

    def test_help_has_no_data_side_effect(self):
        result = self.run_command("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("1024..65535", result.stdout)
        self.assertFalse(self.data.exists())

    def test_rejects_invalid_port_without_creating_data(self):
        for port in (
            "0",
            "1023",
            "65536",
            "8080suffix",
            "8080.5",
            "+8080",
            "-1",
            " 8080",
            "",
            "1e4",
            "99999999999999999999",
        ):
            with self.subTest(port=port):
                result = self.run_command("--port", port)
                self.assertEqual(result.returncode, 1)
                self.assertIn("HTTP port", result.stderr)
                self.assertFalse(self.data.exists())

    def test_accepts_port_boundaries(self):
        for port in ("1024", "65535"):
            with self.subTest(port=port):
                result = self.run_command("--port", port, "--help")
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_missing_value_and_unknown_argument(self):
        self.assertIn("Missing value", self.run_command("--port").stderr)
        self.assertIn("Unknown option", self.run_command("--unknown").stderr)

    def test_password_limits_and_crlf(self):
        self.assertEqual(self.run_command("--init-password", password="short\n").returncode, 1)
        self.assertEqual(
            self.run_command("--init-password", password="x" * 129 + "\n").returncode, 1
        )
        self.assertFalse((self.data / "auth.json").exists())
        result = self.run_command("--init-password", password="x" * 128 + "\r\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        before = (self.data / "auth.json").read_bytes()
        self.assertNotIn(b"x" * 128, before)
        self.assertEqual((self.data / "auth.json").stat().st_mode & 0o777, 0o600)
        duplicate = self.run_command("--init-password", password="another-test-password\n")
        self.assertEqual(duplicate.returncode, 1)
        self.assertEqual((self.data / "auth.json").read_bytes(), before)

    def test_empty_stdin_fails_without_hanging(self):
        result = self.run_command("--init-password", password="")
        self.assertEqual(result.returncode, 1)
        self.assertFalse((self.data / "auth.json").exists())

    def test_interrupt_restores_terminal_echo(self):
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        original = termios.tcgetattr(slave)
        process = subprocess.Popen(
            [str(BINARY), "--data-dir", str(self.data), "--init-password"],
            stdin=slave,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            output = b""
            deadline = time.monotonic() + 5
            while b"Initial admin password" not in output and time.monotonic() < deadline:
                ready, _, _ = select.select([process.stdout], [], [], 0.1)
                if ready:
                    chunk = os.read(process.stdout.fileno(), 256)
                    if not chunk:
                        break
                    output += chunk
            self.assertIn(b"Initial admin password", output)
            self.assertFalse(termios.tcgetattr(slave)[3] & termios.ECHO)
            process.send_signal(signal.SIGINT)
            _, error = process.communicate(timeout=3)
            self.assertEqual(process.returncode, 1)
            self.assertIn(b"interrupted", error)
            self.assertEqual(termios.tcgetattr(slave)[3] & termios.ECHO, original[3] & termios.ECHO)
            self.assertFalse((self.data / "auth.json").exists())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
