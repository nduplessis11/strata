// -----------------------------------------------------------------------------
// engine/base/src/diagnostics.cpp
//
// Purpose:
//   Implements the Diagnostics service for Strata.
//
//   Diagnostics is an explicitly-owned, low-level facility that provides:
//     - Structured logging via a Logger with one or more sinks
//     - Assertion failure handling (debug-only invariants)
//     - Fatal error handling with optional debugger break
//
// Design notes:
//   - No global state or singletons
//   - Intended to be owned by core::Application::Impl
//   - Other systems receive Diagnostics by reference or non-owning pointer
//   - Safe to use in early startup, shutdown, and failure paths
//   - Emission paths avoid throwing and avoid allocation where possible
//
// -----------------------------------------------------------------------------

#include "strata/base/diagnostics.h"

namespace strata::base
{

Diagnostics::Diagnostics() : Diagnostics(Config()) {}

Diagnostics::Diagnostics(Config config)
    : config_(config), logger_(Logger::Config{.min_level = config.min_level})
{
    // Default sink: stderr
    logger_.add_sink(std::make_unique<StderrSink>(StderrSink::Config{
        .include_location  = true,
        .include_thread_id = false,
        .include_timestamp = false,
    }));
}

void Diagnostics::error(std::string_view     category,
                        std::string_view     message,
                        std::source_location location)
{
    logger_.log(LogLevel::Error, category, message, location);

    if (config_.debug_break_on_error && is_debug_build())
    {
        debug_break();
    }
}

void Diagnostics::debug_break_on_error(std::source_location) const noexcept
{
    if (config_.debug_break_on_error && is_debug_build())
    {
        debug_break();
    }
}

[[noreturn]]
void Diagnostics::fatal(std::string_view     category,
                        std::string_view     message,
                        std::source_location location)
{
    logger_.log(LogLevel::Fatal, category, message, location);

    if (config_.debug_break_on_fatal && is_debug_build())
    {
        debug_break();
    }

    std::abort();
}

[[noreturn]]
void Diagnostics::assert_failed(std::string_view     expr_text,
                                std::string_view     message,
                                std::source_location location)
{
    if (!message.empty())
    {
        logger_.log(LogLevel::Fatal,
                    "assert",
                    std::format("Assertion failed: ({}) - {}", expr_text, message),
                    location);
    }
    else
    {
        logger_.log(LogLevel::Fatal,
                    "assert",
                    std::format("Assertion failed: ({})", expr_text),
                    location);
    }

    if (config_.debug_break_on_assert && is_debug_build())
    {
        debug_break();
    }

    std::abort();
}

} // namespace strata::base