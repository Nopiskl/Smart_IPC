#!/usr/bin/env python3
"""使用本机 Firefox Marionette 验证真实页面；无额外 Python/Node 依赖。"""

import base64
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("api_test_support", ROOT / "tests/test_api.py")
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)


class Marionette:
    def __init__(self, port):
        self.connection = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.connection.settimeout(15)
        self.identifier = 0
        self.receive()

    def receive(self):
        prefix = b""
        while not prefix.endswith(b":"):
            byte = self.connection.recv(1)
            if not byte:
                raise RuntimeError("Browser disconnected")
            prefix += byte
        length = int(prefix[:-1])
        payload = b""
        while len(payload) < length:
            chunk = self.connection.recv(length - len(payload))
            if not chunk:
                raise RuntimeError("Browser disconnected")
            payload += chunk
        return json.loads(payload)

    def command(self, name, params=None):
        self.identifier += 1
        payload = json.dumps([0, self.identifier, name, params or {}]).encode()
        self.connection.sendall(str(len(payload)).encode() + b":" + payload)
        while True:
            response = self.receive()
            if response[0] == 1 and response[1] == self.identifier:
                if response[2]:
                    raise RuntimeError(response[2])
                return response[3]

    def script(self, script):
        return self.command(
            "WebDriver:ExecuteScript", {"script": script, "args": [], "newSandbox": False}
        ).get("value")

    def wait(self, expression, seconds=15):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if self.script("return Boolean(" + expression + ");"):
                return
            time.sleep(0.1)
        raise AssertionError("Browser condition timed out: " + expression)

    def screenshot(self, name):
        value = self.command(
            "WebDriver:TakeScreenshot", {"id": None, "full": False, "scroll": False}
        )["value"]
        output = ROOT / "docs/previews" / name
        output.parent.mkdir(exist_ok=True)
        output.write_bytes(base64.b64decode(value))


def main():
    if not shutil.which("firefox"):
        raise SystemExit("Firefox is required for this optional browser check")
    service = support.APITests
    service.setUpClass()
    process = None
    browser = None
    try:
        with tempfile.TemporaryDirectory(prefix="nopiskl-firefox-") as profile:
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            Path(profile, "user.js").write_text(
                f'user_pref("marionette.port", {port});\n'
                'user_pref("browser.shell.checkDefaultBrowser", false);\n'
                'user_pref("browser.startup.homepage_override.mstone", "ignore");\n'
                'user_pref("datareporting.policy.dataSubmissionEnabled", false);\n'
            )
            env = dict(os.environ, MOZ_HEADLESS="1", MOZ_NO_REMOTE="1")
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen(
                    ["firefox", "--headless", "--no-remote", "--marionette", "--profile", profile],
                    env=env,
                    stdout=log,
                    stderr=log,
                )
                end = time.monotonic() + 20
                while time.monotonic() < end:
                    try:
                        browser = Marionette(port)
                        break
                    except OSError:
                        time.sleep(0.2)
                if browser is None:
                    log.seek(0)
                    raise RuntimeError(log.read().decode(errors="replace"))
                browser.command(
                    "WebDriver:NewSession",
                    {"capabilities": {"alwaysMatch": {"acceptInsecureCerts": True}}},
                )
                browser.command("WebDriver:SetWindowRect", {"width": 1440, "height": 1000})
                browser.command("WebDriver:Navigate", {"url": service.base})
                browser.wait("document.getElementById('password') !== null")
                browser.script(
                    "window.qaErrors=[];addEventListener('error',e=>window.qaErrors.push(e.message));addEventListener('unhandledrejection',e=>window.qaErrors.push(String(e.reason)));window.qaOriginalFetch=window.fetch;"
                )
                browser.screenshot("login.png")
                # 登录后信息加载失败时，不应显示一个只初始化了一半的控制台。
                browser.script(
                    "window.fetch=(url,options)=>String(url).endsWith('/device/info')?Promise.reject(new Error('qa-device-info-failure')):window.qaOriginalFetch(url,options);"
                )
                browser.script(
                    "document.getElementById('password').value="
                    + json.dumps(support.PASSWORD)
                    + ";document.getElementById('login-form').requestSubmit();"
                )
                browser.wait(
                    "document.getElementById('login-error').textContent.includes('qa-device-info-failure')"
                )
                assert browser.script(
                    "return document.getElementById('console-view').classList.contains('hidden');"
                )
                browser.script("window.fetch=window.qaOriginalFetch;")
                browser.script(
                    "document.getElementById('password').value="
                    + json.dumps(support.PASSWORD)
                    + ";document.getElementById('login-form').requestSubmit();"
                )
                browser.wait(
                    "!document.getElementById('console-view').classList.contains('hidden') && document.getElementById('media-state').textContent === '主机模拟'"
                )
                browser.screenshot("overview.png")
                browser.script(
                    "document.querySelector('[data-page=video]').click();document.querySelector('#video-form [name=fps]').value='19';document.getElementById('video-form').requestSubmit();"
                )
                browser.wait(
                    "document.getElementById('activity').textContent.includes('视频配置已应用')"
                )
                assert browser.script(
                    "return document.getElementById('rtsp-url').textContent.includes('未启动 RTSP');"
                ), "Simulated RTSP label was overwritten"
                assert browser.script(
                    "return document.getElementById('snapshot-form').elements.format.value === 'bmp';"
                ), "Unsupported snapshot option remains selected"
                browser.script(
                    "document.querySelector('[data-page=snapshot]').click();document.getElementById('capture').click();"
                )
                browser.wait("document.getElementById('preview').naturalWidth === 320")
                browser.screenshot("snapshot.png")
                browser.script(
                    "document.querySelector('[data-page=motor]').click();document.getElementById('motor-form').requestSubmit();"
                )
                browser.wait(
                    "document.getElementById('activity').textContent.includes('电机移动命令已执行')"
                )
                browser.script("document.getElementById('motor-stop').click();")
                browser.wait(
                    "document.getElementById('activity').textContent.includes('电机停止命令已执行')"
                )
                # 退出请求失败也要清理本地画面、捕获异常，并明确提示服务端注销未确认。
                browser.script(
                    "window.fetch=(url,options)=>String(url).endsWith('/auth/logout')?Promise.reject(new Error('qa-logout-failure')):window.qaOriginalFetch(url,options);document.getElementById('logout').click();"
                )
                browser.wait("!document.getElementById('login-view').classList.contains('hidden')")
                browser.wait(
                    "document.getElementById('login-error').textContent.includes('退出请求未完成')"
                )
                assert browser.script(
                    "return !document.getElementById('preview').hasAttribute('src') && !document.getElementById('download').hasAttribute('href');"
                )
                browser.script(
                    "window.fetch=window.qaOriginalFetch;document.getElementById('password').value="
                    + json.dumps(support.PASSWORD)
                    + ";document.getElementById('login-form').requestSubmit();"
                )
                browser.wait(
                    "!document.getElementById('console-view').classList.contains('hidden')"
                )
                assert browser.script(
                    "return document.getElementById('preview').classList.contains('hidden');"
                )

                # 旧会话的迟到 401 不得登出刚建立的新会话，也不得产生未捕获 rejection。
                browser.script(
                    "window.qaRelease=null;window.fetch=(url,options)=>{if(String(url).endsWith('/device/status')&&!window.qaRelease){return new Promise(resolve=>{window.qaRelease=()=>resolve(new Response(JSON.stringify({code:'unauthorized',message:'old session',data:null}),{status:401,headers:{'Content-Type':'application/json'}}));});}return window.qaOriginalFetch(url,options);};document.getElementById('refresh').click();"
                )
                browser.wait("typeof window.qaRelease === 'function'")
                browser.script(
                    "window.fetch=window.qaOriginalFetch;document.getElementById('logout').click();"
                )
                browser.wait("!document.getElementById('login-view').classList.contains('hidden')")
                browser.script(
                    "document.getElementById('password').value="
                    + json.dumps(support.PASSWORD)
                    + ";document.getElementById('login-form').requestSubmit();"
                )
                browser.wait(
                    "!document.getElementById('console-view').classList.contains('hidden')"
                )
                browser.script("window.qaRelease();")
                time.sleep(0.3)
                assert browser.script(
                    "return !document.getElementById('console-view').classList.contains('hidden');"
                ), "Old session response logged out the new session"
                browser.command("WebDriver:SetWindowRect", {"width": 390, "height": 844})
                browser.wait("window.innerWidth <= 450")
                browser.script("document.querySelector('[data-page=overview]').click();")
                browser.wait(
                    "document.querySelector('.nav-item.active').dataset.page === 'overview'"
                )
                time.sleep(0.2)  # 等待窗口尺寸变化后的浏览器重绘。
                browser.screenshot("mobile.png")
                assert browser.script(
                    "return document.documentElement.scrollWidth <= window.innerWidth;"
                ), "Mobile layout overflows"
                browser.script("document.getElementById('logout').click();")
                browser.wait("!document.getElementById('login-view').classList.contains('hidden')")
                assert browser.script("return window.qaErrors.length === 0;"), browser.script(
                    "return window.qaErrors;"
                )
                print(
                    "PASS: Firefox layout, login failure recovery, video config, snapshot, motor, logout failure, relogin cleanup, stale response isolation; no uncaught JS errors"
                )
    finally:
        if browser:
            try:
                browser.command("WebDriver:DeleteSession")
            except (OSError, RuntimeError):
                pass
            browser.connection.close()
        if process:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        service.tearDownClass()


if __name__ == "__main__":
    main()
