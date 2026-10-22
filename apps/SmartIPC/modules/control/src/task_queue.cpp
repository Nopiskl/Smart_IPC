#include "task_queue.h"
#include "nopiskl/common/system.h"
#include <algorithm>

namespace nopiskl {
namespace {
const char *name(JobKind kind) {
    switch (kind) {
    case JobKind::config:
        return "config";
    case JobKind::snapshot:
        return "snapshot";
    case JobKind::motor:
        return "motor";
    }
    throw std::logic_error("Unknown job kind");
}
bool unfinished(const Json &task) {
    return task.at("state") == "queued" || task.at("state") == "running";
}
} // namespace
Json TaskQueue::enqueue(JobKind kind, const std::string &section, const Json &payload,
                        bool priority) {
    if (queue_.size() >= 16 && !priority) {
        throw Error(503, "queue_full", "Control queue is full");
    }
    if (priority) {
        // stop 取消尚未执行的电机动作，防止停止后积压的 move 再次启动电机。
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->kind == JobKind::motor) {
                tasks_.at(it->id)["state"] = "cancelled";
                it = queue_.erase(it);
            } else {
                ++it;
            }
        }
    }
    Job job{random_hex(8), kind, section, payload};
    Json task{{"id", job.id}, {"type", name(kind)}, {"state", "queued"}, {"request", payload}};
    tasks_[job.id] = task;
    order_.push_back(job.id);
    // 优先命令可能越过队头完成，按插入顺序淘汰最早的已完成任务。
    while (order_.size() > 64) {
        auto it = std::find_if(order_.begin(), order_.end(),
                               [&](const std::string &id) { return !unfinished(tasks_.at(id)); });
        if (it == order_.end()) {
            break;
        }
        tasks_.erase(*it);
        order_.erase(it);
    }
    if (priority) {
        queue_.push_front(std::move(job));
    } else {
        queue_.push_back(std::move(job));
    }
    return task;
}
bool TaskQueue::take(Job &job) {
    if (queue_.empty()) {
        return false;
    }
    job = std::move(queue_.front());
    queue_.pop_front();
    tasks_.at(job.id)["state"] = "running";
    return true;
}
void TaskQueue::succeed(const std::string &id, const Json &result) {
    auto &task = tasks_.at(id);
    task["result"] = result;
    task["state"] = "succeeded";
}
void TaskQueue::fail(const std::string &id, const std::exception &exception) {
    const auto *error = dynamic_cast<const Error *>(&exception);
    auto &task = tasks_.at(id);
    task["error"] = {{"code", error ? error->code : "execution_failed"},
                     {"message", exception.what()}};
    task["state"] = "failed";
}
void TaskQueue::cancel_pending() {
    for (const auto &job : queue_) {
        tasks_.at(job.id)["state"] = "cancelled";
    }
    queue_.clear();
}
Json TaskQueue::get(const std::string &id) const {
    const auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        throw Error(404, "task_not_found", "Task not found or expired");
    }
    return it->second;
}
Json TaskQueue::pending() const {
    auto result = Json::array();
    for (const auto &task : tasks_) {
        if (unfinished(task.second)) {
            result.push_back(task.second);
        }
    }
    return result;
}
} // namespace nopiskl
