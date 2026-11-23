#include "VariantComments.h"

#include "Scene.h" // ToLower

#include <algorithm>
#include <array>
#include <iterator>

namespace fr {

bool IsHintClass(std::string_view className) {
    return ToLower(className) == ToLower(kHintEntityClass);
}

bool IsHintQueuePin(std::string_view pin) {
    for (const std::string_view queue : kHintQueuePins) {
        if (pin == queue)
            return true;
    }

    return false;
}

bool IsVitalityResource(uint64_t rid) {
    return rid == kHarebellVitalityTblu || rid == kHarebellVitalityTemp;
}

namespace {

// Their sub-entity ids in scenario_frangipani (0x0000A4945EAFA2B3) and scenario_ambrosia (0x00D47F2B1CBC7BEB), read
// from chunk5patch4 and chunk4patch4.
constexpr std::array<SinLine, 22> kSinLines {{
    {0xB27EA5538110A67Full, "Envy", "AOL_pdlc44007_rivalkillwarn"},
    {0x765D0C68C58D50BCull, "Envy", "AOL_pdlc44008_rivalkillget"},
    {0x029D27ECCC5F82B1ull, "Envy", "AOL_pdlc44009_47killpraise"},
    {0x692279DCAA64DC16ull, "Envy", "AOL_pdlc44010_47killshame"},
    {0x71F4F00AFE1E921Dull, "Envy", "AOL_pdlc44011_rivalwins"},
    {0x82EFFA4F166CE5D4ull, "Envy", "AOL_pdlc44012_47wins"},
    {0x9F53FB20A84618E0ull, "Envy", "pdlc44013_47killrival"},
    {0xD4FB6DA1911D568Cull, "Envy", "pdlc44014_47lowscore"},
    {0x12EB82A1E9B5C8A9ull, "Envy", "pdlc44015_47highscore"},
    {0x2F222770DE7B542Eull, "Lust", "pdlc42000_introdream"},
    {0xFEBA17DF6255C7F8ull, "Lust", "pdlc42001_introrules"},
    {0x1794A090C1496184ull, "Lust", "AOL_pdlc42002_gencluesfound"},
    {0xB762415AFC5B06A4ull, "Lust", "AOL_pdlc42003_mincluesfound"},
    {0x56FCCA93FA322D27ull, "Lust", "AOL_pdlc42004_allcluesfound"},
    {0x370CA98837A49728ull, "Lust", "pdlc42005_bouquetpickup"},
    {0x14B53023733DA9EBull, "Lust", "pdlc42006_admirerid"},
    {0x5D65F53E2388BCC8ull, "Lust", "AOL_pdlc42007_explosivespickup"},
    {0x415498F789742153ull, "Lust", "AOL_pdlc42008_keyspickup"},
    {0xEB239026181459A5ull, "Lust", "AOL_pdlc42009_dartgunpickup"},
    {0x6057A27066AA5B29ull, "Lust", "AOL_pdlc42010_pretenderkill"},
    {0xFC8755BCB2D868B7ull, "Lust", "AOL_pdlc42011_sinchatting"},
    {0xDA198615256F9534ull, "Lust", "pdlc42012_pretenderfail"},
}};

} // namespace

std::span<const SinLine> KnownSinLines() {
    return kSinLines;
}

const SinLine* FindSinLine(uint64_t id, std::span<const SinLine> known) {
    if (id == 0)
        return nullptr;

    for (const SinLine& line : known) {
        if (line.id == id)
            return &line;
    }

    return nullptr;
}

bool IsLineEndPin(std::string_view pin) {
    return std::find(std::begin(kLineEndPins), std::end(kLineEndPins), pin) != std::end(kLineEndPins);
}

StoryFind FindSinLineMutes(const std::vector<int32_t>& lines, size_t count, const std::vector<BriefingLink>& links) {
    StoryFind find = FindStoryMutes(lines, count, links);
    const auto isLine = [&](int32_t index) { return std::find(lines.begin(), lines.end(), index) != lines.end(); };
    std::vector<StoryMute> kept;

    for (StoryMute& mute : find.mutes) {
        // The lines' end links, as if each had played; never one into another line (that would start it past its
        // own block).
        for (const int32_t line : mute.stories) {
            for (size_t i = 0; i < links.size(); ++i) {
                const BriefingLink& end = links[i];

                if (end.kind == BriefingLinkKind::InputForwarding || end.from != line || !IsLineEndPin(end.fromPin) ||
                    end.to < 0 || static_cast<size_t>(end.to) >= count || isLine(end.to))
                    continue;

                if (std::find(mute.resend.begin(), mute.resend.end(), i) == mute.resend.end())
                    mute.resend.push_back(i);
            }
        }

        if (mute.resend.size() > kLineMaxResend) {
            for (size_t k = 0; k < mute.stories.size(); ++k)
                find.skipped.push_back(
                    {mute.stories[k], mute.entity, mute.pin, mute.storyPins[k], StorySkipWhy::TooManyLinks});

            continue;
        }

        kept.push_back(std::move(mute));
    }

    find.mutes = std::move(kept);
    return find;
}

namespace {

// "vitality kept full: 10 pins into helper_harebell_vitality blocked, ..."
std::string VitalityText(const CommentTally& tally) {
    if (tally.vitality == 0)
        return {};

    if (!tally.vitalityWhy.empty() && tally.feedsBlocked == 0)
        return "vitality left running (" + tally.vitalityWhy + "): its thresholds may still raise the music's tension";

    if (tally.feedsBlocked == 0 && tally.feedsLeft > 0)
        return "vitality left running: " + std::to_string(tally.feedsLeft) + " pin" +
               (tally.feedsLeft == 1 ? "" : "s") + " into helper_harebell_vitality could not be blocked";

    if (tally.feedsBlocked == 0)
        return "vitality left running: nothing in its brick feeds helper_harebell_vitality";

    std::string text = "vitality kept full: " + std::to_string(tally.feedsBlocked) + " pin" +
                       (tally.feedsBlocked == 1 ? "" : "s") +
                       " into helper_harebell_vitality blocked, their other "
                       "logic sent on";

    if (tally.feedsLeft > 0)
        text += ", " + std::to_string(tally.feedsLeft) + " left on: it may still drop";

    if (!tally.vitalityWhy.empty())
        text += " (" + tally.vitalityWhy + ")";

    return text;
}

// Sloth's part: "vitality comments muted (12 ZHintEntity, ...)", empty without hints or a vitality helper.
std::string HintText(const CommentTally& tally) {
    const std::string vitality = VitalityText(tally);

    if (tally.hints == 0)
        return vitality.empty() ? std::string()
                                : "vitality comments: no ZHintEntity in this variant's bricks; " + vitality;

    std::string text =
        std::string(tally.hintsMuted > 0 ? "vitality comments muted (" : "vitality comments not muted (");

    if (tally.hintsMuted == tally.hints)
        text += std::to_string(tally.hints) + " ZHintEntity, blocked at " + std::string(kHintStartedPin);
    else
        text += std::to_string(tally.hintsMuted) + " of " + std::to_string(tally.hints) + " ZHintEntity, blocked at " +
                std::string(kHintStartedPin) + "; " + std::to_string(tally.hints - tally.hintsMuted) +
                " could not be: they may still talk";

    return text + (vitality.empty() ? "" : "; " + vitality) + ")";
}

// Envy's and Lust's part: "the sin's lines muted (9, each skipped as if it had played: ...)", empty without lines.
std::string SinLineText(const CommentTally& tally) {
    if (tally.lines == 0)
        return {};

    if (!tally.linesWhy.empty())
        return "the sin's lines not muted (" + std::to_string(tally.lines) + " found; " + tally.linesWhy +
               "): they may still talk";

    std::string text = std::string(tally.linesMuted > 0 ? "the sin's lines muted (" : "the sin's lines not muted (");

    if (tally.linesMuted == tally.lines)
        text += std::to_string(tally.lines);
    else
        text += std::to_string(tally.linesMuted) + " of " + std::to_string(tally.lines);

    text += ", each skipped as if it had played: " + std::to_string(tally.lineFeeds) +
            (tally.lineFeeds == 1 ? " pin that starts" : " pins that start") + " them blocked";

    if (tally.linesMuted < tally.lines)
        text += "; " + std::to_string(tally.lines - tally.linesMuted) + " could not be: they may still talk";

    return text + ")";
}

} // namespace

std::string CommentTallyText(const CommentTally& tally) {
    const std::string hints = HintText(tally);
    const std::string lines = SinLineText(tally);

    if (hints.empty() && lines.empty())
        return "commentary: none in this variant's bricks";

    return hints + (hints.empty() || lines.empty() ? "" : "; ") + lines;
}

} // namespace fr
