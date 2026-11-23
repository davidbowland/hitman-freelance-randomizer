#pragma once

// A JSON member scanner for game event JSON (CpdSet keys at any depth) and the
// credential redaction every logged event JSON goes through.

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// text contains any of parts, ignoring ASCII case (an empty part never matches).
bool ContainsAnyNoCase(std::string_view text, std::span<const std::string_view> parts);

// One member of a JSON object: its key and the raw text of its value (a
// scalar with its quotes, or a whole object or array).
struct JsonMember {
    std::string_view key;
    std::string_view value;
};

// Every member, at any depth, whose key contains any of parts (ignoring ASCII
// case), in text order. Members inside a matched value are not listed again.
// Malformed JSON stops the scan; what was found before is kept.
std::vector<JsonMember> JsonMembersWithKeyContaining(std::string_view json, std::span<const std::string_view> parts);

// --- Credentials: never logged ---

// text contains a word that may carry credentials (token, ticket,
// authorization, session, password, refresh), ignoring ASCII case.
bool HasSensitiveWord(std::string_view text);

// The JSON with the value of every member whose key has a sensitive word (at
// any depth) replaced by "<redacted>". Text after a malformed point is cut
// (with " <cut: malformed>"), so nothing unscanned is returned.
std::string RedactJson(std::string_view json);

} // namespace fr
