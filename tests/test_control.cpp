#include "nopiskl/config/config.h"
#include "nopiskl/common/system.h"
#include "nopiskl/control/control.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace {
using namespace nopiskl;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct TemporaryData {
    char path[64] = "/tmp/nopiskl-control-XXXXXX";
    TemporaryData() {
        if (!mkdtemp(path)) {
            throw std::runtime_error("Cannot create temporary data directory");
        }
    }
    ~TemporaryData() {
        unlink((std::string(path) + "/config.json").c_str());
        rmdir(path);
    }
};

struct Probe {
    std::atomic<bool> wrong_thread{false};
    std::atomic<unsigned> stop_calls{0};
    std::atomic<size_t> snapshot_size{3};
    std::thread::id caller = std::this_thread::get_id();
};

// 刻意不使用 make_media：测试可替换后端及控制层的线程所有权。
class TestMedia : public Media {
public:
    explicit TestMedia(Probe &probe) : probe_(probe) {
    }
    void configure(const Json &config) override {
        check_owner();
        config_ = config;
        if (config.at("video").at("fps") == 20) {
            throw std::runtime_error("Injected backend configuration failure");
        }
    }
    void step() override {
        check_owner();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Image snapshot(const Json &) override {
        check_owner();
        std::vector<unsigned char> bytes(probe_.snapshot_size, 7);
        return {"application/octet-stream", std::move(bytes)};
    }
    Json status() const override {
        return {{"ready", !config_.is_null()}, {"backend", "injected"}};
    }

private:
    Probe &probe_;
    Json config_;
    std::thread::id owner_;
    void check_owner() {
        const auto current = std::this_thread::get_id();
        if (owner_ == std::thread::id{}) {
            owner_ = current;
        }
        if (current == probe_.caller || current != owner_) {
            probe_.wrong_thread = true;
        }
    }
};

class TestMotor : public Motor {
public:
    explicit TestMotor(Probe &probe) : probe_(probe) {
    }
    Json execute(const Json &command) override {
        if (command.at("action") == "stop") {
            ++probe_.stop_calls;
        }
        return status();
    }
    Json status() override {
        return {{"available", true}, {"backend", "injected"}, {"moving", false}};
    }

private:
    Probe &probe_;
};

Json await_task(Control &control, const Json &submitted) {
    const auto deadline = now_ms() + 3000;
    while (now_ms() < deadline) {
        const auto task = control.task(submitted.at("id"));
        if (task.at("state") == "succeeded" || task.at("state") == "failed") {
            return task;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Control task timed out");
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "Expected default configuration path");
        TemporaryData data;
        auto config = load_config(argv[1], data.path);
        Probe probe;
        bool rejected = false;
        try {
            Control invalid(config, data.path, nullptr, std::make_unique<TestMotor>(probe));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "Null backend must be rejected");

        Control control(config, data.path, std::make_unique<TestMedia>(probe),
                        std::make_unique<TestMotor>(probe));
        control.start();
        auto failure = await_task(control, control.submit_config("video", {{"fps", 20}}));
        require(failure.at("state") == "failed", "Backend error must fail the task");
        require(control.config("video") == config.at("video"), "Failed patch must roll back");

        auto success = await_task(control, control.submit_config("video", {{"fps", 24}}));
        require(success.at("state") == "succeeded", "Worker must recover after backend failure");
        require(load_config(argv[1], data.path).at("video").at("fps") == 24,
                "Successful patch must be persisted");
        auto capture = await_task(control, control.submit_snapshot());
        require(capture.at("state") == "succeeded", "Injected media must produce a snapshot");
        auto image = control.image(capture.at("result").at("snapshot_id"));
        require(image->bytes == std::vector<unsigned char>({7, 7, 7}),
                "Control must use the injected media backend");
        require(!capture.at("result").contains("url"), "Control must not generate HTTP URLs");
        auto next_capture = await_task(control, control.submit_snapshot());
        require(next_capture.at("state") == "succeeded", "Second snapshot must succeed");
        rejected = false;
        try {
            control.image(capture.at("result").at("snapshot_id"));
        } catch (const Error &error) {
            rejected = error.code == "snapshot_not_found";
        }
        require(rejected, "Previous snapshot must be evicted");
        require(image->bytes == std::vector<unsigned char>({7, 7, 7}),
                "Eviction must preserve an image already held by a downloader");
        probe.snapshot_size = 8 * 1024 * 1024 + 1;
        auto oversized = await_task(control, control.submit_snapshot());
        require(oversized.at("state") == "failed" &&
                    oversized.at("error").at("code") == "snapshot_too_large",
                "Oversized snapshot must fail");
        require(control.image(next_capture.at("result").at("snapshot_id"))->bytes == image->bytes,
                "Rejected snapshot must preserve the last successful image");
        require(control.status().at("motor").at("backend") == "injected",
                "Control must use the injected motor backend");
        control.stop();
        require(!probe.wrong_thread, "Media operations must stay on one worker thread");
        require(probe.stop_calls == 1, "Shutdown must stop the injected motor");
        std::cout << "PASS: injected backends, rollback, persistence, thread ownership, shutdown\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
