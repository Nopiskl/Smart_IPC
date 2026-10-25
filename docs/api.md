# SmartIPC HTTP API v1

基础地址：`http://<设备IP>:8080/api/v1`。除登录外，所有 API 均要求会话 Cookie；静态页面无需登录即可加载。写操作额外要求 `X-CSRF-Token`。当前只接受带 `Content-Length` 的 JSON body，不接受 chunked JSON 上传。

## 响应与状态

```json
{"code":"ok","message":"OK","request_id":"12","data":{}}
```

错误使用同一外层结构，`data` 为 null，`code` 表示错误类型。HTTP 400：参数/JSON 错误；401：需要登录；403：CSRF 无效；404：资源不存在或已淘汰；405：方法不支持；413：body 过大/不支持的传输；415：Content-Type 不支持；429：登录限速；501：电机未接入；503：队列满。异步任务执行失败通过 task 的 `state=failed` 和 `error` 表示，不能只看提交接口是否返回 202。

写配置、截图、电机操作返回 202：

```json
{"code":"ok","message":"OK","request_id":"15","data":{"id":"0123456789abcdef","type":"config","state":"queued","request":{"fps":20}}}
```

轮询 `GET /tasks/0123456789abcdef`，状态依次为 `queued`、`running`、`succeeded` 或 `failed`；停止命令可能将待执行电机任务标为 `cancelled`。任务成功有 `result`，失败有 `error.code/message`。只保留最近 64 个任务。

## 路由

| 方法 | 路径 | 参数/结果 |
| --- | --- | --- |
| POST | `/auth/login` | `{username,password}`；返回 csrf_token，Cookie 中设置会话 |
| GET | `/auth/session` | 查询账户及当前 csrf_token |
| POST | `/auth/logout` | 注销当前会话 |
| PUT | `/auth/password` | `{current_password,new_password}`；成功后全部会话失效 |
| GET | `/device/info` | 品牌、版本、架构、内核、IPv4 接口 |
| GET | `/device/status` | media、motor、运行时长、内存、pending_tasks |
| GET | `/capabilities` | 编码、截图格式、电机后端、视频参数约束、模拟标识 |
| GET / PUT | `/video/config` | 已生效视频配置 / 修改字段 |
| GET / PUT | `/ai/config` | 已生效 AI 配置 / 修改字段 |
| GET / PUT | `/snapshot/config` | 已生效截图配置 / 修改字段 |
| POST | `/snapshots` | 对当前帧截图；返回 task，不需要 body |
| GET | `/snapshots/{id}` | 成功截图的二进制 JPEG/BMP，需要 Cookie |
| GET | `/tasks/{id}` | 查询任务 |
| GET | `/motor/status` | 后端可用性、模拟标志、运动状态 |
| POST | `/motor/commands` | 电机命令；返回 task |

## 可修改字段

`PUT` 为字段合并，未提供字段保留。未知字段、null 删除、类型错误均拒绝。

| 分组 | 字段及约束 |
| --- | --- |
| video | `codec:"h264"`；`width`:320..1920 且 16 对齐；`height`:240..1080 且偶数；`fps`:1..30；`bitrate_kbps`:128..16384；`gop`:1..300，整数 |
| ai | `enabled/overlay`:boolean；`confidence/nms`：0 到 1 之间的数，不含端点 |
| snapshot | `format`:jpeg/bmp；`quality`:1..100 整数；`overlay`:boolean |

视频范围表示参数校验边界，传感器、固件与内存实际能否建立通道，以 task 结果为准。模型/标签路径、HTTP 监听、RTSP 端口及设备名称通过离线配置设定，不开放 HTTP 任意文件路径写入。

## 外部客户端示例

以下 Python 标准库示例从终端读取密码，不将密码写进命令行历史，依次登录、修改帧率、查询任务。

```python
import getpass, http.cookiejar, json, time, urllib.request
base = 'http://192.168.1.100:8080/api/v1/'
client = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
csrf = ''
def call(path, method='GET', data=None):
    headers = {'X-CSRF-Token': csrf}
    if data is not None:
        headers['Content-Type'] = 'application/json'
    request = urllib.request.Request(base + path, method=method, headers=headers,
        data=None if data is None else json.dumps(data).encode())
    with client.open(request, timeout=10) as response:
        return json.load(response)['data']
csrf = call('auth/login', 'POST', {'username':'admin','password':getpass.getpass()})['csrf_token']
task = call('video/config', 'PUT', {'fps':20})
while True:
    task = call('tasks/' + task['id'])
    if task['state'] in ('succeeded','failed','cancelled'):
        print(task)
        break
    time.sleep(0.3)
```

也可通过 `curl -b cookies.txt -H "X-CSRF-Token: <登录返回的令牌>" -H 'Content-Type: application/json' -X PUT -d '{"fps":20}' http://<设备IP>:8080/api/v1/video/config` 调用已登录会话。

会话最多 4 个，创建后 30 分钟失效；重启不保留。登录失败有递增等待，最大 30 秒。凭据使用 Argon2id（8 MiB、3 轮、1 通道、128 位随机盐），适配嵌入式资源预算。HTTP 当前未启用 TLS，部署边界见 [部署访问](build_and_deploy.md#部署访问)。
