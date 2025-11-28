#include "platform/clock.h"

#include <chrono>

namespace strata::platform {
    std::uint64_t monotonic_milliseconds() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
} // namespace strata::platform
