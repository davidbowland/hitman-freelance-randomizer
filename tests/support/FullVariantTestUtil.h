#pragma once

// A full variant by key, as the mod finds one (fr::FullVariantOfKey).

#include "LandslideFull.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace fr::test {

inline const FullVariant& Full(std::string_view key) {
    const FullVariant* variant = FullVariantOfKey(key);

    if (!variant)
        throw std::logic_error("no full variant " + std::string(key));

    return *variant;
}

} // namespace fr::test
