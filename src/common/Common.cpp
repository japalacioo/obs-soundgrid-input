#include "Clock.h"
#include "Types.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace sg {

const char *platformName(Platform p)
{
    switch (p) {
    case Platform::Windows:
        return "windows";
    case Platform::MacOS:
        return "macos";
    default:
        return "other";
    }
}

const char *severityName(Severity s)
{
    switch (s) {
    case Severity::Info:
        return "INFO";
    case Severity::Warn:
        return "WARN";
    case Severity::Critical:
        return "CRITICAL";
    }
    return "?";
}

const char *confidenceName(Confidence c)
{
    switch (c) {
    case Confidence::Measured:
        return "medido";
    case Confidence::Estimated:
        return "estimado";
    case Confidence::NotObservable:
        return "no_observable";
    }
    return "?";
}

double linearToDbfs(double linear)
{
    if (!(linear > 1e-10))
        return -200.0;
    return 20.0 * std::log10(linear);
}

double dbfsToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

std::string wallClockIso8601Utc()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

} // namespace sg
