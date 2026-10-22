# 外部电机控制协议与适配

## 当前状态

HTTP 协议解析、校验、排队、任务查询和模拟运动已实现。默认 `disabled` 后端返回 501 `motor_unavailable`。当前尚无物理电机引脚配置或驱动，不会把模拟成功报告为硬件动作完成。

显式使用 `--simulate-motor` 启用 `simulated` 后端，状态包含 `simulated:true`。模拟后端按持续时间自动结束，可验证上层控制流程。

## 移动

`POST /api/v1/motor/commands`：

```json
{"action":"move","axis":"pan","direction":1,"speed":0.3,"duration_ms":500}
```

| 字段 | 定义 |
| --- | --- |
| action | move / stop |
| axis | pan：水平；tilt：俯仰 |
| direction | 整数 +1 / -1，具体物理方向由驱动说明 |
| speed | 大于 0 且不大于 1 的归一化速度，驱动转换为频率/占空比/串口速度 |
| duration_ms | 整数 1..5000；后端应保证超时停止 |

当前 move 接口不声称支持绝对角度、编码器反馈、回零或硬件限位。新增能力应先在 capabilities 中声明，并扩展文档。

## 停止与完成语义

```json
{"action":"stop"}
```

stop 优先排队，同时取消尚未执行的电机命令。正在执行的媒体取帧、编码或配置操作仍可能使停止处理产生延迟。它是软件控制命令，物理限位与急停应由硬件后端保证。

move 任务成功表示后端接受并启动命令；是否仍在运动通过 `/motor/status` 的 `moving` 查询，不能将入队或启动成功当作机械到位。硬件定位任务若需要“到位”语义，应新增反馈状态。

## 后端接入

实现 `apps/SmartIPC/modules/control/include/nopiskl/control/motor.h` 的 `Motor::execute` 与 `Motor::status`。可在 `apps/SmartIPC/modules/control/src/motor.cpp` 的默认工厂中注册，或在 `apps/SmartIPC/src/application.cpp` 直接创建后端并以 `std::unique_ptr<Motor>` 传给 Control。UART/PWM/GPIO 的设备路径、引脚复用、极性、限位及驱动时序由确定的硬件方案提供。

不要在 HTTP 回调中直接读写电机。后端的 execute 应有超时且快速返回；持续运动、看门狗和反馈可以由驱动内部的受控线程或非阻塞状态机处理，析构和程序退出时应停止输出。

后续串口协议可以将收到的帧转换为同一套命令对象，再调用 Control；届时定义帧头、版本、长度、序号、校验和重发规则。当前没有接入任何虚构的厂商串口帧格式。
