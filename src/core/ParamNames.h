#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace fr {

// Name of an EDifficultyParameter value; empty if unknown.
std::string_view ParamName(int32_t value);

// Name for logs: the enumerator name, "<unknown parameter N>", or
// "<unreadable m_eParameter>" when empty.
std::string ParamLabel(std::optional<int32_t> value);

// EDifficultyParameter value for an exact (case-sensitive) enumerator name.
std::optional<int32_t> ParamFromName(std::string_view name);

// pro.repo cuts "Parameter" texts to this many characters.
constexpr size_t kRepoParameterMaxChars = 63;

// The parameter a pro.repo "Parameter" text names: an exact enumerator name,
// or, for a text of exactly kRepoParameterMaxChars characters (cut by
// pro.repo), the only enumerator whose name starts with it.
std::optional<int32_t> ParamFromRepoName(std::string_view text);

} // namespace fr
