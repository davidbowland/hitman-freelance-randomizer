// Concealable mace (see MaceConceal.h). Every engine read and write goes
// through ProRepository's guarded steps.

#include "MaceConceal.h"

#include "ConcealableMace.h"
#include "LogTopics.h"
#include "ModLog.h"

#include <exception>
#include <string_view>
#include <vector>

namespace {

const char* FlagText(int8_t p_Flag) {
    return p_Flag > 0 ? "true" : p_Flag == 0 ? "false" : "-";
}

std::string QuotedOrMissing(const ZString* p_Value, const char* p_Text) {
    return p_Value ? fmt::format("\"{}\"", p_Text) : std::string("(no string key)");
}

std::string TitleOf(const RepoConcealEntry& p_Entry) {
    if (p_Entry.title[0] && p_Entry.commonName[0])
        return fmt::format("{} / {}", p_Entry.title, p_Entry.commonName);
    return p_Entry.title[0] ? p_Entry.title : p_Entry.commonName;
}

bool IsMace(const RepoConcealEntry& p_Entry) {
    return fr::IsMeleeIcon(p_Entry.icon) && fr::IsMaceName(p_Entry.title, p_Entry.commonName);
}

} // namespace

bool MaceConceal::AnyWritten() const {
    for (const auto& [s_Guid, s_Mace] : m_Maces)
        if (s_Mace.itemSize.written || s_Mace.holster.written)
            return true;

    return false;
}

bool MaceConceal::Track(Key& p_Key, ZString* p_Value, const char* p_Current, const Mace& p_Mace, const char* p_Name) {
    if (!p_Value)
        return false;

    const std::string_view s_Current(p_Current);

    if (p_Key.tracked) {
        if (s_Current == p_Key.vanillaText || (p_Key.written && s_Current == *p_Key.written))
            return true;

        LogTo(fr::LogTopic::Weapons,
              "concealable mace: \"{}\": {} changed outside this mod (\"{}\" -> \"{}\"); taken as its vanilla value",
              p_Mace.title, p_Name, p_Key.vanillaText, s_Current);
    }

    if (const uint32_t s_Code = m_Repo.CopyString(&p_Key.vanilla, p_Value)) {
        LogTo(fr::LogTopic::Weapons,
              "concealable mace: \"{}\": copying the vanilla {} faulted (exception 0x{:08X}); not changed",
              p_Mace.title, p_Name, s_Code);
        p_Key.tracked = false;
        return false;
    }

    p_Key.tracked = true;
    p_Key.vanillaText = std::string(s_Current);
    p_Key.written.reset();
    return true;
}

void MaceConceal::Apply(const char* p_When) noexcept {
    try {
        std::string s_Why;

        if (!m_Repo.Ready(s_Why)) {
            LogTo(fr::LogTopic::Weapons, "concealable mace: {}: pro.repo: {}; nothing changed", p_When, s_Why);
            return;
        }

        std::vector<RepoConcealEntry> s_Entries;
        size_t s_Overflow = 0;
        size_t s_Scanned = 0;

        if (const uint32_t s_Code = m_Repo.ScanConcealItems(s_Entries, &s_Overflow, &s_Scanned)) {
            LogTo(fr::LogTopic::Weapons,
                  "concealable mace: {}: the pro.repo scan faulted (exception 0x{:08X}) after {} entries; nothing "
                  "changed",
                  p_When, s_Code, s_Scanned);
            return;
        }

        // The reference: melee items 47 can pocket (the maces left out, they
        // may hold what this wrote). Their ItemSize counts pick the target;
        // any entry holding a target text lends its ZString.
        std::map<std::string, size_t> s_Sizes;
        std::map<std::string, size_t> s_ReferenceKinds; // "ItemSize, HitmanCanConceal, frisked" of the reference
        std::map<std::string, ZString*> s_SizeValues;
        ZString* s_HolsterValue = nullptr;
        size_t s_Reference = 0;
        std::vector<const RepoConcealEntry*> s_Maces;

        for (const RepoConcealEntry& s_Entry : s_Entries) {
            if (IsMace(s_Entry)) {
                s_Maces.push_back(&s_Entry);
                continue;
            }

            if (s_Entry.itemSizeValue && !s_SizeValues.count(s_Entry.itemSize))
                s_SizeValues.emplace(s_Entry.itemSize, s_Entry.itemSizeValue);

            if (s_Entry.holsterValue && !s_HolsterValue && s_Entry.holster == fr::kHolsteringAllowed)
                s_HolsterValue = s_Entry.holsterValue;

            if (!fr::IsMeleeIcon(s_Entry.icon) || s_Entry.holster != fr::kHolsteringAllowed || !s_Entry.itemSizeValue)
                continue;

            ++s_Reference;
            ++s_Sizes[s_Entry.itemSize];
            ++s_ReferenceKinds[fmt::format("{}, HitmanCanConceal {}, frisked {}", s_Entry.itemSize,
                                           FlagText(s_Entry.canConceal), FlagText(s_Entry.frisked))];
        }

        if (s_Maces.empty()) {
            if (!m_LoggedNone) {
                m_LoggedNone = true;
                LogTo(fr::LogTopic::Weapons,
                      "concealable mace: {}: no melee item named \"mace\" among {} pro.repo items; nothing changed",
                      p_When, s_Entries.size());
            }
            return;
        }

        const std::string s_Target = fr::PickConcealedItemSize(s_Sizes);
        const auto s_SizeIt = s_SizeValues.find(s_Target);
        ZString* s_SizeValue = s_SizeIt != s_SizeValues.end() ? s_SizeIt->second : nullptr;

        std::string s_Census;
        for (const auto& [s_Kind, s_Count] : s_ReferenceKinds)
            s_Census += fmt::format("{}{} ({})", s_Census.empty() ? "" : "; ", s_Kind, s_Count);

        size_t s_Written = 0;
        size_t s_Held = 0;
        size_t s_Failed = 0;
        std::string s_Plan = fmt::format("target ItemSize \"{}\"{}, HolsterAbility \"{}\"{}", s_Target,
                                         s_SizeValue ? "" : " (NO entry holds it: not written)", fr::kHolsteringAllowed,
                                         s_HolsterValue ? "" : " (NO entry holds it: not written)");

        for (const RepoConcealEntry* s_Entry : s_Maces) {
            Mace& s_Mace = m_Maces[s_Entry->guid];
            s_Mace.title = TitleOf(*s_Entry);

            const bool s_SizeTracked =
                Track(s_Mace.itemSize, s_Entry->itemSizeValue, s_Entry->itemSize, s_Mace, "ItemSize");
            const bool s_HolsterTracked =
                Track(s_Mace.holster, s_Entry->holsterValue, s_Entry->holster, s_Mace, "HolsterAbility");

            fr::ConcealInput s_Input;
            s_Input.hasItemSize = s_SizeTracked && s_SizeValue;
            s_Input.itemSize = s_Entry->itemSize;
            s_Input.hasHolster = s_HolsterTracked && s_HolsterValue;
            s_Input.holster = s_Entry->holster;
            const fr::ConcealChange s_Change = fr::PlanConceal(s_Input, s_Target);

            const auto s_WriteKey = [&](bool p_Write, Key& p_Key, ZString* p_To, ZString* p_From,
                                        std::string_view p_Text) {
                if (!p_Key.tracked || !p_From)
                    return;

                if (!p_Write) {
                    ++s_Held;
                    if (p_Key.vanillaText != p_Text)
                        p_Key.written = std::string(p_Text);
                    return;
                }

                if (const uint32_t s_Code = m_Repo.CopyString(p_To, p_From)) {
                    ++s_Failed;
                    Log("concealable mace: {}: \"{}\": the write FAULTED (exception 0x{:08X})", p_When, s_Mace.title,
                        s_Code);
                    return;
                }

                ++s_Written;
                p_Key.written = p_Key.vanillaText != p_Text ? std::optional<std::string>(p_Text) : std::nullopt;
            };

            s_WriteKey(s_Change.itemSize, s_Mace.itemSize, s_Entry->itemSizeValue, s_SizeValue, s_Target);
            s_WriteKey(s_Change.holster, s_Mace.holster, s_Entry->holsterValue, s_HolsterValue, fr::kHolsteringAllowed);

            s_Plan += fmt::format(
                "; {} \"{}\": ItemSize {} -> \"{}\", HolsterAbility {} -> \"{}\" (vanilla \"{}\" / \"{}\"; "
                "HitmanCanConceal {}, IsCheckedWhenFrisked {}, unchanged)",
                fr::FormatGuid(s_Entry->guid), s_Mace.title, QuotedOrMissing(s_Entry->itemSizeValue, s_Entry->itemSize),
                s_Mace.itemSize.written ? *s_Mace.itemSize.written : s_Mace.itemSize.vanillaText,
                QuotedOrMissing(s_Entry->holsterValue, s_Entry->holster),
                s_Mace.holster.written ? *s_Mace.holster.written : s_Mace.holster.vanillaText,
                s_Mace.itemSize.vanillaText, s_Mace.holster.vanillaText, FlagText(s_Entry->canConceal),
                FlagText(s_Entry->frisked));
        }

        if (s_Plan != m_LoggedPlan) {
            m_LoggedPlan = s_Plan;
            LogTo(fr::LogTopic::Weapons, "concealable mace: {}: {} mace(s); {}", p_When, s_Maces.size(), s_Plan);
            LogTo(fr::LogTopic::Weapons,
                  "concealable mace: reference: {} pocketable melee items (HolsterAbility eHolsteringAllowed): {}",
                  s_Reference, s_Census.empty() ? std::string("none") : s_Census);
        }

        LogTo(fr::LogTopic::Weapons, "concealable mace: {}: {} values written, {} already held{}", p_When, s_Written,
              s_Held, s_Failed ? fmt::format(", {} writes FAULTED", s_Failed) : std::string());
    }
    catch (const std::exception& e) {
        Log("concealable mace: {}: failed: {}", p_When, e.what());
    }
    catch (...) {
        Log("concealable mace: {}: failed: unknown exception", p_When);
    }
}

void MaceConceal::Restore(const char* p_When) noexcept {
    try {
        if (!AnyWritten())
            return;

        std::string s_Why;

        if (!m_Repo.Ready(s_Why)) {
            LogTo(fr::LogTopic::Weapons, "concealable mace: {}: pro.repo: {}; nothing put back", p_When, s_Why);
            return;
        }

        std::vector<RepoConcealEntry> s_Entries;
        size_t s_Overflow = 0;
        size_t s_Scanned = 0;

        if (const uint32_t s_Code = m_Repo.ScanConcealItems(s_Entries, &s_Overflow, &s_Scanned)) {
            Log("concealable mace: {}: the pro.repo scan faulted (exception 0x{:08X}); nothing put back", p_When,
                s_Code);
            return;
        }

        size_t s_Restored = 0;
        size_t s_Failed = 0;

        const auto s_RestoreKey = [&](Key& p_Key, ZString* p_Value, const char* p_Current) {
            if (!p_Key.written)
                return;

            if (p_Value && fr::ShouldRestoreText(p_Current, *p_Key.written, p_Key.vanillaText)) {
                if (m_Repo.CopyString(p_Value, &p_Key.vanilla))
                    ++s_Failed;
                else
                    ++s_Restored;
            }

            p_Key.written.reset();
        };

        for (const RepoConcealEntry& s_Entry : s_Entries) {
            const auto s_It = m_Maces.find(s_Entry.guid);

            if (s_It == m_Maces.end())
                continue;

            s_RestoreKey(s_It->second.itemSize, s_Entry.itemSizeValue, s_Entry.itemSize);
            s_RestoreKey(s_It->second.holster, s_Entry.holsterValue, s_Entry.holster);
        }

        // A mace no longer in pro.repo: nothing to put back.
        for (auto& [s_Guid, s_Mace] : m_Maces) {
            s_Mace.itemSize.written.reset();
            s_Mace.holster.written.reset();
        }

        m_LoggedPlan.clear();
        LogTo(fr::LogTopic::Weapons, "concealable mace: {}: {} values put back to vanilla{}", p_When, s_Restored,
              s_Failed ? fmt::format(", {} writes faulted", s_Failed) : std::string());
    }
    catch (const std::exception& e) {
        Log("concealable mace: {}: restore failed: {}", p_When, e.what());
    }
    catch (...) {
        Log("concealable mace: {}: restore failed: unknown exception", p_When);
    }
}
