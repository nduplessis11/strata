#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace strata::platform {
    std::optional<std::string> load_text_file(std::string_view path);
}
