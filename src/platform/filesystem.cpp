#include "platform/filesystem.h"

#include <fstream>
#include <sstream>

namespace strata::platform {
    std::optional<std::string> load_text_file(std::string_view path) {
        std::ifstream file{ std::string{ path } };
        if (!file.is_open()) {
            return std::nullopt;
        }

        std::ostringstream oss;
        oss << file.rdbuf();
        return oss.str();
    }
} // namespace strata::platform
