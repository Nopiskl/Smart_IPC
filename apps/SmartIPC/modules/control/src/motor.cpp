#include "nopiskl/control/motor.h"
#include "nopiskl/common/system.h"
#include <set>
namespace nopiskl {
void validate_motor_command(const Json &c) {
    if (!c.is_object() || !c.contains("action") || !c.at("action").is_string()) {
        throw Error(400, "invalid_command", "action is required");
    }
    for (auto it = c.begin(); it != c.end(); ++it) {
        if (!std::set<std::string>{"action", "axis", "direction", "speed", "duration_ms"}.count(
                it.key())) {
            throw Error(400, "invalid_command", "Unknown motor field: " + it.key());
        }
    }
    if (c.at("action") == "stop") {
        if (c.size() != 1) {
            throw Error(400, "invalid_command", "stop accepts only action");
        }
        return;
    }
    if (c.at("action") != "move") {
        throw Error(400, "invalid_command", "Supported actions: move, stop");
    }
    if (!c.contains("axis") || (c.at("axis") != "pan" && c.at("axis") != "tilt") ||
        !c.contains("direction") || !c.at("direction").is_number_integer() ||
        (c.at("direction") != 1 && c.at("direction") != -1) || !c.contains("speed") ||
        !c.at("speed").is_number() || c.at("speed").get<double>() <= 0 ||
        c.at("speed").get<double>() > 1 || !c.contains("duration_ms") ||
        !c.at("duration_ms").is_number_integer() || c.at("duration_ms").get<int64_t>() < 1 ||
        c.at("duration_ms").get<int64_t>() > 5000) {
        throw Error(400, "invalid_command",
                    "move requires axis, direction +/-1, speed (0,1], duration_ms 1..5000");
    }
}
class DisabledMotor : public Motor {
public:
    Json execute(const Json &) override {
        throw Error(501, "motor_unavailable", "No physical motor backend installed");
    }
    Json status() override {
        return {{"backend", "disabled"}, {"available", false}, {"moving", false}};
    }
};
class SimulatedMotor : public Motor {
    uint64_t deadline_ = 0;
    Json command_ = Json::object();

public:
    Json execute(const Json &c) override {
        validate_motor_command(c);
        if (c.at("action") == "stop") {
            deadline_ = 0;
        } else {
            command_ = c;
            deadline_ = now_ms() + c.at("duration_ms").get<unsigned>();
        }
        return status();
    }
    Json status() override {
        return {{"backend", "simulated"},
                {"available", true},
                {"simulated", true},
                {"moving", now_ms() < deadline_},
                {"last_command", command_}};
    }
};
std::unique_ptr<Motor> make_motor(bool simulated) {
    if (simulated) {
        return std::unique_ptr<Motor>(new SimulatedMotor);
    }
    return std::unique_ptr<Motor>(new DisabledMotor);
}
} // namespace nopiskl
