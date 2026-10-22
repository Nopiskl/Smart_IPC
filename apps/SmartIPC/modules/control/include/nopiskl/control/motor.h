#pragma once
#include "nopiskl/common/types.h"
#include <memory>

namespace nopiskl {
// 将来 UART/PWM 驱动只需实现此接口。所有角度/限位/回零能力由具体驱动声明。
class Motor {
public:
    virtual ~Motor() = default;
    virtual Json execute(const Json &command) = 0;
    virtual Json status() = 0;
};
std::unique_ptr<Motor> make_motor(bool simulated);
void validate_motor_command(const Json &command);
} // namespace nopiskl
