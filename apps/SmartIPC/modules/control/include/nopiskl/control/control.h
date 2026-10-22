#pragma once
#include "nopiskl/media/media.h"
#include "nopiskl/control/motor.h"

namespace nopiskl {
// 有界队列：接收成功表示排队成功；最终执行结果必须查询 task。
class Control {
public:
    // 后端由应用组装，控制线程独占使用；可注入真实驱动或测试替身。
    Control(Json config, std::string data_dir, std::unique_ptr<Media> media,
            std::unique_ptr<Motor> motor);
    ~Control();
    void start();
    void stop();
    Json config(const std::string &section);
    Json status();
    Json submit_config(const std::string &section, const Json &patch);
    Json submit_snapshot();
    Json submit_motor(const Json &command);
    Json task(const std::string &id);
    std::shared_ptr<const Image> image(const std::string &id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nopiskl
