#pragma once

// Every EDifficultyParameter enumerator (but EDP_Invalid and EDP_COUNT) by
// name, and every rollable dial, both in enum order, through the lookups the
// mod uses (fr::ParamName, fr::FindDial).

#include "Dials.h"
#include "ParamNames.h"

#include <Glacier/Enums.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace fr::test {

inline std::vector<std::pair<std::string_view, int32_t>> AllParams() {
    std::vector<std::pair<std::string_view, int32_t>> out;

    for (int32_t value = 1; value < static_cast<int32_t>(EDifficultyParameter::EDP_COUNT); ++value) {
        if (const std::string_view name = ParamName(value); !name.empty())
            out.emplace_back(name, value);
    }

    return out;
}

inline std::vector<DialSpec> AllDials() {
    std::vector<DialSpec> out;

    for (const auto& [name, value] : AllParams()) {
        if (const DialSpec* dial = FindDial(value))
            out.push_back(*dial);
    }

    return out;
}

} // namespace fr::test
