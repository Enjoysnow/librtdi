#pragma once

#include <string_view>

namespace librtdi {

enum class lifetime_kind {
    singleton,
    transient
};

constexpr std::string_view to_string(lifetime_kind lt) noexcept {
    constexpr std::string_view names[] = {"singleton", "transient"};
    return names[static_cast<int>(lt)];
}

} // namespace librtdi
