#include "JsonScan.h"

#include "TextNoCase.h"

#include <algorithm>
#include <array>
#include <utility>

namespace fr {

namespace {

constexpr std::array<std::string_view, 6> kSensitiveWords {
    "token", "ticket", "authorization", "session", "password", "refresh",
};

// --- JSON scanning ---

struct Cursor {
    std::string_view s;
    size_t i = 0;

    void SkipSpace() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
    }
};

// Past the string starting at c.i (on its opening quote); false when unterminated.
bool SkipString(Cursor& c) {
    ++c.i;

    while (c.i < c.s.size()) {
        if (c.s[c.i] == '\\') {
            c.i += 2;
            continue;
        }

        if (c.s[c.i++] == '"')
            return true;
    }

    return false;
}

bool ScanValue(Cursor& c, std::span<const std::string_view> parts, std::vector<JsonMember>& out, int depth);

bool ScanObject(Cursor& c, std::span<const std::string_view> parts, std::vector<JsonMember>& out, int depth) {
    ++c.i; // {
    c.SkipSpace();

    if (c.i < c.s.size() && c.s[c.i] == '}') {
        ++c.i;
        return true;
    }

    while (c.i < c.s.size()) {
        c.SkipSpace();

        if (c.i >= c.s.size() || c.s[c.i] != '"')
            return false;

        const size_t keyStart = c.i + 1;

        if (!SkipString(c))
            return false;

        const std::string_view key = c.s.substr(keyStart, c.i - 1 - keyStart);
        c.SkipSpace();

        if (c.i >= c.s.size() || c.s[c.i] != ':')
            return false;

        ++c.i;
        c.SkipSpace();
        const size_t valueStart = c.i;
        const bool matched = ContainsAnyNoCase(key, parts);
        std::vector<JsonMember> ignored;

        if (!ScanValue(c, parts, matched ? ignored : out, depth + 1))
            return false;

        if (matched)
            out.push_back({key, c.s.substr(valueStart, c.i - valueStart)});

        c.SkipSpace();

        if (c.i < c.s.size() && c.s[c.i] == ',') {
            ++c.i;
            continue;
        }

        if (c.i < c.s.size() && c.s[c.i] == '}') {
            ++c.i;
            return true;
        }

        return false;
    }

    return false;
}

bool ScanArray(Cursor& c, std::span<const std::string_view> parts, std::vector<JsonMember>& out, int depth) {
    ++c.i; // [
    c.SkipSpace();

    if (c.i < c.s.size() && c.s[c.i] == ']') {
        ++c.i;
        return true;
    }

    while (c.i < c.s.size()) {
        c.SkipSpace();

        if (!ScanValue(c, parts, out, depth + 1))
            return false;

        c.SkipSpace();

        if (c.i < c.s.size() && c.s[c.i] == ',') {
            ++c.i;
            continue;
        }

        if (c.i < c.s.size() && c.s[c.i] == ']') {
            ++c.i;
            return true;
        }

        return false;
    }

    return false;
}

bool ScanValue(Cursor& c, std::span<const std::string_view> parts, std::vector<JsonMember>& out, int depth) {
    if (depth > 64 || c.i >= c.s.size())
        return false;

    switch (c.s[c.i]) {
        case '{': return ScanObject(c, parts, out, depth);
        case '[': return ScanArray(c, parts, out, depth);
        case '"': return SkipString(c);
        default: break;
    }

    // A number, true, false or null: up to the next delimiter.
    const size_t start = c.i;

    while (c.i < c.s.size() && c.s[c.i] != ',' && c.s[c.i] != '}' && c.s[c.i] != ']' && c.s[c.i] != ' ' &&
           c.s[c.i] != '\t' && c.s[c.i] != '\r' && c.s[c.i] != '\n')
        ++c.i;

    return c.i > start;
}

// Scans json fully; returns false when malformed, with the end of the
// well-formed prefix in *scanned.
bool ScanAll(std::string_view json, std::span<const std::string_view> parts, std::vector<JsonMember>& out,
             size_t* scanned) {
    Cursor c {json};
    c.SkipSpace();
    bool ok = c.i < json.size() && ScanValue(c, parts, out, 0);
    *scanned = c.i;
    c.SkipSpace();
    return ok && c.i == json.size(); // trailing text is not scanned
}

} // namespace

bool HasSensitiveWord(std::string_view text) {
    return ContainsAnyNoCase(text, kSensitiveWords);
}

std::string RedactJson(std::string_view json) {
    std::vector<JsonMember> members;
    size_t scanned = 0;
    const bool ok = ScanAll(json, kSensitiveWords, members, &scanned);
    std::string out;
    size_t at = 0;

    for (const JsonMember& member : members) {
        const size_t begin = static_cast<size_t>(member.value.data() - json.data());
        out.append(json.substr(at, begin - at));
        out += "\"<redacted>\"";
        at = begin + member.value.size();
    }

    if (ok) {
        out.append(json.substr(at));
        return out;
    }

    // Malformed: keep only the scanned prefix, and only if it has no
    // sensitive word left (a key the scan had not finished).
    const std::string_view rest = json.substr(at, scanned > at ? scanned - at : 0);

    if (!HasSensitiveWord(rest))
        out.append(rest);

    return out + " <cut: malformed>";
}

bool ContainsAnyNoCase(std::string_view text, std::span<const std::string_view> parts) {
    return std::any_of(parts.begin(), parts.end(),
                       [&](std::string_view part) { return FindNoCase(text, part) != std::string_view::npos; });
}

std::vector<JsonMember> JsonMembersWithKeyContaining(std::string_view json, std::span<const std::string_view> parts) {
    std::vector<JsonMember> out;
    Cursor c {json};
    c.SkipSpace();

    if (c.i < json.size())
        ScanValue(c, parts, out, 0);

    return out;
}

} // namespace fr
