#include "AlertSink.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sg {

namespace {
std::filesystem::path utf8Path(const std::string &s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
} // namespace

std::string jsonEscape(const std::string &s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof(b), "\\u%04x", c);
                o += b;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o;
}

std::string alertEventToJson(const AlertEvent &e)
{
    std::ostringstream os;
    os << '{' << "\"time\":\"" << jsonEscape(e.wallTime) << "\",\"id\":" << e.id << ",\"rule\":\"" << ruleCode(e.rule)
       << "\",\"title\":\"" << jsonEscape(ruleTitle(e.rule)) << "\",\"severity\":\"" << severityName(e.severity)
       << "\",\"state\":\"" << alertStateName(e.state) << "\",\"transition\":\"" << jsonEscape(e.transition)
       << "\",\"started_at_ns\":" << e.startedAtNs << ",\"ended_at_ns\":" << e.endedAtNs << ",\"message\":\""
       << jsonEscape(e.message) << "\",\"evidence\":{\"observed\":\"" << jsonEscape(e.evidence.observed)
       << "\",\"threshold\":\"" << jsonEscape(e.evidence.threshold) << "\",\"cause\":\""
       << jsonEscape(e.evidence.cause) << "\",\"action\":\"" << jsonEscape(e.evidence.action)
       << "\",\"confidence\":\"" << confidenceName(e.evidence.confidence) << "\",\"source\":\""
       << jsonEscape(e.evidence.measurementSource) << "\"},\"correlation_id\":" << e.correlationId
       << ",\"secondary\":" << (e.secondary ? "true" : "false") << ",\"notify\":" << (e.notify ? "true" : "false")
       << ",\"platform\":\"" << platformName(e.platform) << "\",\"source\":\"" << jsonEscape(e.affectedSource)
       << "\"}";
    return os.str();
}

// ------------------------------------------------------------------ AlertHistory

void AlertHistory::onEvent(const AlertEvent &e)
{
    std::lock_guard lk(mu_);
    events_.push_back(e);
    while (events_.size() > capacity_)
        events_.pop_front();
}

std::vector<AlertEvent> AlertHistory::recent(std::size_t max) const
{
    std::lock_guard lk(mu_);
    std::vector<AlertEvent> v;
    for (auto it = events_.rbegin(); it != events_.rend() && v.size() < max; ++it)
        v.push_back(*it);
    return v;
}

void AlertHistory::clear()
{
    std::lock_guard lk(mu_);
    events_.clear();
}

// ------------------------------------------------------------------ JsonlAlertLog

JsonlAlertLog::JsonlAlertLog(std::string path, uint64_t maxBytes, int keepFiles)
    : path_(std::move(path)), maxBytes_(maxBytes), keep_(keepFiles < 1 ? 1 : keepFiles)
{
    std::error_code ec;
    std::filesystem::create_directories(utf8Path(path_).parent_path(), ec);
}

void JsonlAlertLog::rotateIfNeeded(std::size_t incoming)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = utf8Path(path_);
    const auto size = fs::exists(p, ec) ? fs::file_size(p, ec) : 0;
    if (ec || size + incoming <= maxBytes_)
        return;
    // alerts.jsonl -> alerts.1.jsonl -> ... -> alerts.<keep>.jsonl (se elimina el más antiguo)
    const auto dot = path_.find_last_of('.');
    const auto slash = path_.find_last_of("/\\");
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    const std::string base = hasExt ? path_.substr(0, dot) : path_;
    const std::string ext = hasExt ? path_.substr(dot) : std::string();
    auto numbered = [&](int i) { return utf8Path(base + "." + std::to_string(i) + ext); };
    fs::remove(numbered(keep_), ec);
    for (int i = keep_ - 1; i >= 1; --i)
        if (fs::exists(numbered(i), ec))
            fs::rename(numbered(i), numbered(i + 1), ec);
    fs::rename(p, numbered(1), ec);
    ++rotations_;
}

void JsonlAlertLog::writeLine(const std::string &line)
{
    std::lock_guard lk(mu_);
    rotateIfNeeded(line.size() + 1);
    std::ofstream f(utf8Path(path_), std::ios::app | std::ios::binary);
    if (f)
        f << line << '\n';
}

void JsonlAlertLog::onEvent(const AlertEvent &e)
{
    writeLine(alertEventToJson(e));
}

// ------------------------------------------------------------------ AlertDispatcher

AlertDispatcher::AlertDispatcher(std::size_t capacity, bool startThread) : capacity_(capacity)
{
    if (startThread)
        thread_ = std::thread([this] { run(); });
}

AlertDispatcher::~AlertDispatcher()
{
    {
        std::lock_guard lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void AlertDispatcher::addSink(std::shared_ptr<IAlertSink> s)
{
    std::lock_guard lk(sinksMu_);
    sinks_.push_back(std::move(s));
}

bool AlertDispatcher::publish(const AlertEvent &e)
{
    {
        std::lock_guard lk(mu_);
        if (queue_.size() >= capacity_) {
            dropped_.fetch_add(1);
            return false;
        }
        queue_.push_back(e);
    }
    cv_.notify_one();
    return true;
}

std::size_t AlertDispatcher::drain()
{
    std::deque<AlertEvent> batch;
    {
        std::lock_guard lk(mu_);
        batch.swap(queue_);
    }
    std::lock_guard lk(sinksMu_);
    for (const auto &e : batch) {
        for (auto &s : sinks_) {
            try {
                s->onEvent(e);
            } catch (...) {
                // Un sumidero defectuoso nunca debe tumbar el monitor ni OBS.
            }
        }
        delivered_.fetch_add(1);
    }
    return batch.size();
}

void AlertDispatcher::flush()
{
    if (!thread_.joinable()) {
        drain();
        return;
    }
    for (int i = 0; i < 500; ++i) {
        {
            std::lock_guard lk(mu_);
            if (queue_.empty())
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::lock_guard lk(sinksMu_); // espera a que termine el lote en curso
}

std::size_t AlertDispatcher::queued() const
{
    std::lock_guard lk(mu_);
    return queue_.size();
}

void AlertDispatcher::run()
{
    for (;;) {
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty())
                return;
        }
        drain();
    }
}

} // namespace sg
