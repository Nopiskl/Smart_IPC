#include "nopiskl/control/control.h"
#include "nopiskl/common/system.h"
#include "config_service.h"
#include "snapshot_store.h"
#include "task_queue.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace nopiskl {
struct Control::Impl {
    std::mutex mutex;
    ConfigService config;
    TaskQueue tasks;
    SnapshotStore snapshots;
    std::unique_ptr<Media> media;
    std::unique_ptr<Motor> motor;
    Json media_state = {{"ready", false}, {"state", "starting"}};
    Json motor_state;
    std::atomic<bool> running{false};
    std::thread worker;
    uint64_t started = now_ms();

    Impl(Json initial, std::string data_dir, std::unique_ptr<Media> media_backend,
         std::unique_ptr<Motor> motor_backend)
        : config(std::move(initial), std::move(data_dir)), media(std::move(media_backend)),
          motor(std::move(motor_backend)) {
        if (!media || !motor) {
            throw std::invalid_argument("Control requires media and motor backends");
        }
        motor_state = motor->status();
    }
    Json enqueue(JobKind kind, const std::string &section, const Json &payload,
                 bool priority = false) {
        std::lock_guard<std::mutex> lock(mutex);
        return tasks.enqueue(kind, section, payload, priority);
    }
    void execute(const Job &job) {
        Json result;
        switch (job.kind) {
        case JobKind::config:
            result = config.apply(job.section, job.payload, *media);
            break;
        case JobKind::snapshot: {
            auto image = media->snapshot(config.section("snapshot"));
            std::lock_guard<std::mutex> lock(mutex);
            result = snapshots.put(job.id, std::move(image));
            break;
        }
        case JobKind::motor:
            result = motor->execute(job.payload);
            break;
        }
        auto next_media = media->status();
        auto next_motor = motor->status();
        std::lock_guard<std::mutex> lock(mutex);
        // 状态与成功结果一起发布，查询到 succeeded 后可以读取对应设备状态。
        media_state = std::move(next_media);
        motor_state = std::move(next_motor);
        tasks.succeed(job.id, result);
    }
    void run() {
        try {
            media->configure(config.current());
        } catch (const std::exception &error) {
            std::cerr << "[nopiskl] Media unavailable: " << error.what() << '\n';
        }
        while (running) {
            Job job;
            bool have_job;
            {
                std::lock_guard<std::mutex> lock(mutex);
                have_job = tasks.take(job);
            }
            if (have_job) {
                try {
                    execute(job);
                } catch (const std::exception &error) {
                    std::lock_guard<std::mutex> lock(mutex);
                    tasks.fail(job.id, error);
                }
            }
            try {
                media->step();
            } catch (const std::exception &error) {
                std::cerr << "[nopiskl] frame: " << error.what() << '\n';
            }
            auto next_media = media->status();
            auto next_motor = motor->status();
            const bool ready = next_media.value("ready", false);
            {
                std::lock_guard<std::mutex> lock(mutex);
                media_state = std::move(next_media);
                motor_state = std::move(next_motor);
            }
            if (!ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        try {
            motor->execute({{"action", "stop"}});
        } catch (...) {
        }
        std::lock_guard<std::mutex> lock(mutex);
        tasks.cancel_pending();
    }
};

Control::Control(Json config, std::string data_dir, std::unique_ptr<Media> media,
                 std::unique_ptr<Motor> motor)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(data_dir), std::move(media),
                                   std::move(motor))) {
}
Control::~Control() {
    stop();
}
void Control::start() {
    if (impl_->running.exchange(true)) {
        return;
    }
    try {
        impl_->worker = std::thread(&Impl::run, impl_.get());
    } catch (...) {
        impl_->running = false;
        throw;
    }
}
void Control::stop() {
    impl_->running = false;
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
}
Json Control::config(const std::string &section) {
    return impl_->config.section(section);
}
Json Control::status() {
    // 系统文件读取放在任务锁之外，避免状态轮询阻塞任务发布。
    auto memory = memory_info();
    auto config = impl_->config.current();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return {{"media", impl_->media_state},
            {"motor", impl_->motor_state},
            {"uptime_seconds", (now_ms() - impl_->started) / 1000},
            {"device_name", config.at("device_name")},
            {"memory", memory},
            {"pending_tasks", impl_->tasks.pending()}};
}
Json Control::submit_config(const std::string &section, const Json &patch) {
    impl_->config.validate(section, patch);
    return impl_->enqueue(JobKind::config, section, patch);
}
Json Control::submit_snapshot() {
    return impl_->enqueue(JobKind::snapshot, "", Json::object());
}
Json Control::submit_motor(const Json &command) {
    validate_motor_command(command);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->motor_state.value("available", false)) {
            throw Error(501, "motor_unavailable", "No physical motor backend installed");
        }
    }
    return impl_->enqueue(JobKind::motor, "", command, command.at("action") == "stop");
}
Json Control::task(const std::string &id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->tasks.get(id);
}
std::shared_ptr<const Image> Control::image(const std::string &id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshots.get(id);
}
} // namespace nopiskl
