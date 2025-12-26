// -----------------------------------------------------------------------------
// engine/base/include/strata/base/diagnostics.h
//
// Purpose:
//   Explicitly-owned diagnostics: logging + assertions, with zero global state.
//   A Diagnostics instance owns a Logger and sinks, and higher layers pass a
//   Diagnostics& down intentionally.
//
//  Notes:
//    - No Vulkan / OS dependencies
//    - Designed to be owned by core::Application::Impl.
//    - Platform/backend/renderer store non-owning pointers.
//
// -----------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace strata::base
{

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

[[nodiscard]]
constexpr std::string_view to_string(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return "Trace";
    case LogLevel::Debug:
        return "Debug";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warn:
        return "Warn";
    case LogLevel::Error:
        return "Error";
    case LogLevel::Fatal:
        return "Fatal";
    }
    return "Unknown";
}

[[nodiscard]]
constexpr bool is_debug_build() noexcept
{
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

inline void debug_break() noexcept
{
#if defined(_MSC_VER)
    __debugbreak();
#else
    std::raise(SIGTRAP);
#endif
}

struct LogRecord
{
    LogLevel                              level{};
    std::string                           category{};
    std::string                           message{};
    std::source_location                  location{};
    std::chrono::system_clock::time_point timestamp{};
    std::thread::id                       thread_id{};
};

class ILogSink
{
  public:
    virtual ~ILogSink()                         = default;
    virtual void write(LogRecord const& record) = 0;
};

class StderrSink final : public ILogSink
{
  public:
    struct Config
    {
        bool include_location{true};
        bool include_thread_id{false};
        bool include_timestamp{false};
    };

    StderrSink() = default;
    explicit StderrSink(Config config) : config_(config) {}

    void write(LogRecord const& record) override
    {
        std::scoped_lock lock{mutex_};

        auto const level_sv = to_string(record.level);

        std::fprintf(stderr,
                     "[%.*s][%.*s] %.*s",
                     static_cast<std::int32_t>(record.category.size()),
                     record.category.data(),
                     static_cast<std::int32_t>(level_sv.size()),
                     level_sv.data(),
                     static_cast<std::int32_t>(record.message.size()),
                     record.message.data());

        if (config_.include_location)
        {
            std::fprintf(stderr, " (%s:%u)", record.location.file_name(), record.location.line());
        }

        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

  private:
    Config     config_{};
    std::mutex mutex_{};
};

class Logger final
{
  public:
    struct Config
    {
        LogLevel min_level{is_debug_build() ? LogLevel::Debug : LogLevel::Info};
    };

    Logger() = default;
    explicit Logger(Config config) : min_level_(config.min_level) {}

    void add_sink(std::unique_ptr<ILogSink> sink)
    {
        if (!sink)
        {
            return;
        }
        sinks_.push_back(std::move(sink));
    }

    [[nodiscard]]
    bool should_log(LogLevel level) const noexcept
    {
        return static_cast<std::int32_t>(level) >= static_cast<std::int32_t>(min_level_);
    }

    void set_min_level(LogLevel level) noexcept
    {
        min_level_ = level;
    }

    void log(LogLevel             level,
             std::string_view     category,
             std::string_view     message,
             std::source_location location = std::source_location::current())
    {
        if (!should_log(level))
        {
            return;
        }

        LogRecord record{};
        record.level     = level;
        record.category  = std::string(category);
        record.message   = std::string(message);
        record.location  = location;
        record.timestamp = std::chrono::system_clock::now();
        record.thread_id = std::this_thread::get_id();

        for (auto const& sink : sinks_)
        {
            sink->write(record);
        }
    }

    template <typename... Args>
    void logf(LogLevel                    level,
              std::string_view            category,
              std::source_location        location,
              std::format_string<Args...> fmt,
              Args&&... args)
    {
        // Caller should guard with should_log(level) to avoid formatting when disabled.
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        log(level, category, msg, location);
    }

  private:
    LogLevel                               min_level_{LogLevel::Info};
    std::vector<std::unique_ptr<ILogSink>> sinks_{};
};

class Diagnostics final
{
  public:
    struct Config
    {
        LogLevel min_level{is_debug_build() ? LogLevel::Debug : LogLevel::Info};

        bool debug_break_on_error{is_debug_build()};
        bool debug_break_on_assert{is_debug_build()};
        bool debug_break_on_fatal{is_debug_build()};
    };

    Diagnostics();
    explicit Diagnostics(Config config);

    [[nodiscard]]
    Logger& logger() noexcept
    {
        return logger_;
    }
    [[nodiscard]]
    Logger const& logger() const noexcept
    {
        return logger_;
    }

    void error(std::string_view     category,
               std::string_view     message,
               std::source_location location = std::source_location::current());

    [[noreturn]]
    void fatal(std::string_view     category,
               std::string_view     message,
               std::source_location location = std::source_location::current());

    void debug_break_on_error(
        std::source_location location = std::source_location::current()) const noexcept;

    [[noreturn]]
    void assert_failed(std::string_view     expr_text,
                       std::string_view     message,
                       std::source_location location);

  private:
    Config config_{};
    Logger logger_{};
};

#if !defined(NDEBUG)

#define STRATA_ASSERT(diag, expr)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            (diag).assert_failed(#expr, std::string_view{}, std::source_location::current());      \
        }                                                                                          \
    } while (0)

#define STRATA_ASSERT_MSG(diag, expr, fmt, ...)                                                    \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            auto _strata_msg = std::format((fmt)__VA_OPT__(, ) __VA_ARGS__);                       \
            (diag).assert_failed(#expr, _strata_msg, std::source_location::current());             \
        }                                                                                          \
    } while (0)

#else

#define STRATA_ASSERT(diag, expr)                                                                  \
    do                                                                                             \
    {                                                                                              \
        (void)sizeof(expr);                                                                        \
    } while (0)

#define STRATA_ASSERT_MSG(diag, expr, fmt, ...)                                                    \
    do                                                                                             \
    {                                                                                              \
        (void)sizeof(expr);                                                                        \
    } while (0)

#endif

// Logging macros: guard formatting + args evaluation by should_log().
#define STRATA_LOGF(logger, level, category, fmt, ...)                                             \
    do                                                                                             \
    {                                                                                              \
        auto& _strata_logger = (logger);                                                           \
        if (_strata_logger.should_log((level)))                                                    \
        {                                                                                          \
            _strata_logger.logf((level),                                                           \
                                (category),                                                        \
                                std::source_location::current(),                                   \
                                (fmt)__VA_OPT__(, ) __VA_ARGS__);                                  \
        }                                                                                          \
    } while (0)

#define STRATA_LOG_DEBUG(logger, category, fmt, ...)                                               \
    STRATA_LOGF((logger),                                                                          \
                ::strata::base::LogLevel::Debug,                                                   \
                (category),                                                                        \
                (fmt)__VA_OPT__(, ) __VA_ARGS__)
#define STRATA_LOG_INFO(logger, category, fmt, ...)                                                \
    STRATA_LOGF((logger),                                                                          \
                ::strata::base::LogLevel::Info,                                                    \
                (category),                                                                        \
                (fmt)__VA_OPT__(, ) __VA_ARGS__)
#define STRATA_LOG_WARN(logger, category, fmt, ...)                                                \
    STRATA_LOGF((logger),                                                                          \
                ::strata::base::LogLevel::Warn,                                                    \
                (category),                                                                        \
                (fmt)__VA_OPT__(, ) __VA_ARGS__)
#define STRATA_LOG_ERROR(logger, category, fmt, ...)                                               \
    STRATA_LOGF((logger),                                                                          \
                ::strata::base::LogLevel::Error,                                                   \
                (category),                                                                        \
                (fmt)__VA_OPT__(, ) __VA_ARGS__)

} // namespace strata::base