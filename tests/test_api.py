#!/usr/bin/env python3
"""真实启动主机服务，验证鉴权、协议、异步任务、配置持久化及模拟电机。"""

import concurrent.futures
import http.cookiejar
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build/host/apps/SmartIPC/SmartIPC"
PASSWORD = "test-only-SmartIPC-2026"


class Client:
    def __init__(self, base):
        self.base = base
        self.csrf = ""
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    def call(
        self, path, method="GET", data=None, csrf=True, raw=None, content_type="application/json"
    ):
        headers = {}
        if csrf:
            headers["X-CSRF-Token"] = self.csrf
        payload = json.dumps(data).encode() if data is not None else raw
        if payload is not None:
            headers["Content-Type"] = content_type
        request = urllib.request.Request(
            self.base + path, method=method, headers=headers, data=payload
        )
        try:
            with self.opener.open(request, timeout=10) as response:
                return response.status, response.headers, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.headers, error.read()

    def json(self, path, method="GET", data=None, **options):
        status, headers, body = self.call("/api/v1/" + path, method, data, **options)
        return status, json.loads(body)

    def login(self, password=PASSWORD):
        status, result = self.json(
            "auth/login", "POST", {"username": "admin", "password": password}
        )
        if status != 200:
            raise AssertionError(result)
        self.csrf = result["data"]["csrf_token"]
        return result

    def task(self, task):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            status, result = self.json("tasks/" + task["id"])
            if status != 200:
                raise AssertionError(result)
            task = result["data"]
            if task["state"] in ("succeeded", "failed", "cancelled"):
                return task
            time.sleep(0.03)
        raise AssertionError("Task timeout")


class APITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.exists():
            raise RuntimeError("Build first: ./build.sh --host")
        cls.temporary = tempfile.TemporaryDirectory(prefix="nopiskl-api-")
        cls.data = Path(cls.temporary.name)
        subprocess.run(
            [str(BINARY), "--data-dir", str(cls.data), "--init-password"],
            input=PASSWORD + "\n",
            text=True,
            check=True,
            capture_output=True,
        )
        cls.start_server()

    @classmethod
    def start_server(cls, simulated_motor=True):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            cls.port = sock.getsockname()[1]
        cls.base = f"http://127.0.0.1:{cls.port}"
        args = [
            str(BINARY),
            "--data-dir",
            str(cls.data),
            "--config",
            str(ROOT / "apps/SmartIPC/config/default.json"),
            "--web-root",
            str(ROOT / "apps/SmartIPC/web"),
            "--port",
            str(cls.port),
        ]
        if simulated_motor:
            args.append("--simulate-motor")
        cls.log = tempfile.TemporaryFile()
        cls.process = subprocess.Popen(args, stdout=cls.log, stderr=cls.log)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if cls.process.poll() is not None:
                cls.log.seek(0)
                raise RuntimeError(cls.log.read().decode())
            try:
                with urllib.request.urlopen(cls.base, timeout=1) as response:
                    if response.status == 200:
                        return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("HTTP startup timeout")

    @classmethod
    def stop_server(cls):
        cls.process.terminate()
        cls.process.wait(timeout=10)
        if cls.process.returncode != 0:
            cls.log.seek(0)
            raise AssertionError(cls.log.read().decode())
        cls.log.close()

    @classmethod
    def tearDownClass(cls):
        if cls.process.poll() is None:
            cls.stop_server()
        cls.temporary.cleanup()

    def setUp(self):
        self.client = Client(self.base)
        self.client.login()

    def test_01_authentication_and_static_assets(self):
        anonymous = Client(self.base)
        code, result = anonymous.json("device/status")
        self.assertEqual(code, 401)
        self.assertEqual(result["code"], "unauthorized")
        for path, mime in [
            ("/", "text/html"),
            ("/app.js", "application/javascript"),
            ("/style.css", "text/css"),
        ]:
            code, headers, data = anonymous.call(path)
            self.assertEqual(code, 200)
            self.assertIn(mime, headers["Content-Type"])
            self.assertGreater(len(data), 100)
            self.assertIn("frame-ancestors 'none'", headers["Content-Security-Policy"])
        for path in ("/data/auth.json", "/../data/auth.json", "/config/default.json"):
            self.assertEqual(anonymous.call(path)[0], 404)
        login = self.client.login()
        self.assertNotIn("token", login["data"])
        self.assertTrue(any(cookie.has_nonstandard_attr("HttpOnly") for cookie in self.client.jar))

    def test_02_csrf_and_input_validation(self):
        self.assertEqual(self.client.json("video/config", "PUT", {"fps": 20}, csrf=False)[0], 403)
        for value in (
            {"width": 721},
            {"fps": 0},
            {"fps": "20"},
            {"width": None},
            {"codec": "h265"},
            {"unknown": 1},
        ):
            self.assertEqual(self.client.json("video/config", "PUT", value)[0], 400, value)
        self.assertEqual(self.client.json("ai/config", "PUT", {"confidence": 1})[0], 400)
        self.assertEqual(self.client.json("snapshot/config", "PUT", {"overlay": 1})[0], 400)
        self.assertEqual(self.client.json("video/config", "PUT", raw=b"{broken")[0], 400)
        self.assertEqual(
            self.client.json("video/config", "PUT", raw=b'{"fps":20}', content_type="text/plain")[
                0
            ],
            415,
        )
        nested = '{"fps":' + "[" * 12 + "0" + "]" * 12 + "}"
        self.assertEqual(self.client.json("video/config", "PUT", raw=nested.encode())[0], 400)
        huge = b'{"unknown":"' + b"x" * 9000 + b'"}'
        self.assertEqual(self.client.json("video/config", "PUT", raw=huge)[0], 413)

    def test_03_config_task_and_persistence(self):
        code, response = self.client.json("video/config", "PUT", {"fps": 17})
        self.assertEqual(code, 202)
        task = self.client.task(response["data"])
        self.assertEqual(task["state"], "succeeded", task)
        self.assertTrue(task["result"]["rtsp_reconnect_required"])
        self.assertEqual(self.client.json("video/config")[1]["data"]["fps"], 17)
        saved = json.loads((self.data / "config.json").read_text())
        self.assertEqual(saved["video"]["fps"], 17)
        self.assertEqual((self.data / "config.json").stat().st_mode & 0o777, 0o600)

    def test_04_concurrent_patch_merge(self):
        def update(patch):
            client = Client(self.base)
            client.login()
            code, value = client.json("ai/config", "PUT", patch)
            self.assertEqual(code, 202)
            self.assertEqual(client.task(value["data"])["state"], "succeeded")

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(update, [{"confidence": 0.37}, {"nms": 0.59}]))
        # 并发登录可能触发 4 会话淘汰，重新登录后读最终生效值。
        self.client.login()
        settings = self.client.json("ai/config")[1]["data"]
        self.assertEqual(settings["confidence"], 0.37)
        self.assertEqual(settings["nms"], 0.59)

    def test_04b_config_save_failure_rolls_back(self):
        saved = self.data / "config.json"
        backup = self.data / "config.backup.json"
        saved.rename(backup)
        saved.mkdir()  # 构造实际文件保存错误，不依赖进程 UID 或 chmod 行为。
        try:
            code, result = self.client.json("video/config", "PUT", {"fps": 18})
            self.assertEqual(code, 202)
            task = self.client.task(result["data"])
            self.assertEqual(task["state"], "failed")
            self.assertEqual(self.client.json("video/config")[1]["data"]["fps"], 17)
        finally:
            saved.rmdir()
            backup.rename(saved)

    def test_05_snapshot_binary_and_eviction(self):
        code, response = self.client.json("snapshots", "POST")
        self.assertEqual(code, 202)
        result = self.client.task(response["data"])
        self.assertEqual(result["state"], "succeeded", result)
        url = result["result"]["url"]
        code, headers, data = self.client.call(url)
        self.assertEqual(code, 200)
        self.assertEqual(headers["Content-Type"], "image/bmp")
        self.assertEqual(data[:2], b"BM")
        self.assertEqual(len(data), result["result"]["size"])
        self.assertEqual(Client(self.base).call(url)[0], 401)
        next_task = self.client.json("snapshots", "POST")[1]["data"]
        self.assertEqual(self.client.task(next_task)["state"], "succeeded")
        self.assertEqual(self.client.call(url)[0], 404)

    def test_06_motor_command_timeout_and_stop(self):
        command = {
            "action": "move",
            "axis": "pan",
            "direction": 1,
            "speed": 0.3,
            "duration_ms": 150,
        }
        task = self.client.json("motor/commands", "POST", command)[1]["data"]
        self.assertEqual(self.client.task(task)["state"], "succeeded")
        time.sleep(0.3)
        state = self.client.json("motor/status")[1]["data"]
        self.assertTrue(state["simulated"])
        self.assertFalse(state["moving"])
        command["duration_ms"] = 5000
        self.assertEqual(
            self.client.task(self.client.json("motor/commands", "POST", command)[1]["data"])[
                "state"
            ],
            "succeeded",
        )
        stop = self.client.json("motor/commands", "POST", {"action": "stop"})[1]["data"]
        self.assertEqual(self.client.task(stop)["state"], "succeeded")
        self.assertFalse(self.client.json("motor/status")[1]["data"]["moving"])
        command["duration_ms"] = 5001
        self.assertEqual(self.client.json("motor/commands", "POST", command)[0], 400)

    def test_07_capabilities_and_logout(self):
        capabilities = self.client.json("capabilities")[1]["data"]
        self.assertTrue(capabilities["simulated_media"])
        self.assertEqual(capabilities["codecs"], [])
        self.assertEqual(capabilities["snapshot_formats"], ["bmp"])
        self.assertEqual(self.client.json("auth/logout", "POST")[0], 200)
        self.assertEqual(self.client.json("device/info")[0], 401)

    def test_08_restart_and_disabled_motor(self):
        self.stop_server()
        self.start_server(simulated_motor=False)
        # setUp 使用的旧地址不再可用，重建客户端，凭据及配置应持久化。
        self.client = Client(self.base)
        self.client.login()
        self.assertEqual(self.client.json("video/config")[1]["data"]["fps"], 17)
        self.assertEqual(self.client.json("motor/commands", "POST", {"action": "stop"})[0], 501)
        self.assertFalse(self.client.json("motor/status")[1]["data"]["available"])

    def test_07b_stop_cancels_pending_moves(self):
        command = {
            "action": "move",
            "axis": "pan",
            "direction": 1,
            "speed": 0.3,
            "duration_ms": 5000,
        }
        moves = []
        for _ in range(10):
            code, value = self.client.json("motor/commands", "POST", command)
            self.assertEqual(code, 202)
            moves.append(value["data"])
        stop = self.client.json("motor/commands", "POST", {"action": "stop"})[1]["data"]
        self.assertEqual(self.client.task(stop)["state"], "succeeded")
        states = [self.client.json("tasks/" + move["id"])[1]["data"]["state"] for move in moves]
        self.assertIn("cancelled", states)
        self.assertFalse(self.client.json("motor/status")[1]["data"]["moving"])

    def test_07c_task_history_is_bounded(self):
        first = None
        for _ in range(66):
            code, value = self.client.json("motor/commands", "POST", {"action": "stop"})
            self.assertEqual(code, 202)
            task = value["data"]
            first = first or task["id"]
            self.assertEqual(self.client.task(task)["state"], "succeeded")
        self.assertEqual(self.client.json("tasks/" + first)[0], 404)

    def test_09_password_change_and_sessions(self):
        new_password = "test-only-changed-SmartIPC"
        code, _ = self.client.json(
            "auth/password", "PUT", {"current_password": PASSWORD, "new_password": new_password}
        )
        self.assertEqual(code, 200)
        self.assertEqual(self.client.json("device/info")[0], 401)
        self.client.login(new_password)
        auth = (self.data / "auth.json").read_text()
        self.assertNotIn(new_password, auth)
        self.assertEqual((self.data / "auth.json").stat().st_mode & 0o777, 0o600)
        # 不再进行成功登录，验证失败后的即时请求受到限速。
        self.assertEqual(
            self.client.json("auth/login", "POST", {"username": "admin", "password": PASSWORD})[0],
            401,
        )
        self.assertEqual(
            self.client.json("auth/login", "POST", {"username": "admin", "password": PASSWORD})[0],
            429,
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
