#pragma once
#include "nopiskl/common/types.h"
#include <deque>
#include <map>

namespace nopiskl {
enum class JobKind { config, snapshot, motor };
struct Job {
    std::string id;
    JobKind kind = JobKind::config;
    std::string section;
    Json payload;
};
// 队列与任务历史由 Control 的同一把锁保护，保证状态发布与任务完成一致。
class TaskQueue {
public:
    Json enqueue(JobKind kind, const std::string &section, const Json &payload, bool priority);
    bool take(Job &job);
    void succeed(const std::string &id, const Json &result);
    void fail(const std::string &id, const std::exception &error);
    void cancel_pending();
    Json get(const std::string &id) const;
    Json pending() const;

private:
    std::deque<Job> queue_;
    std::map<std::string, Json> tasks_;
    std::deque<std::string> order_;
};
} // namespace nopiskl
