// Weapon rarity shuffle (see WeaponRarityShuffle.h). Every engine read and
// write goes through ProRepository's guarded steps.

#include "WeaponRarityShuffle.h"

#include "LogTopics.h"
#include "ModLog.h"

#include <chrono>
#include <exception>
#include <string>
#include <vector>

namespace {

double MsSince(std::chrono::steady_clock::time_point p_Start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_Start).count();
}

bool IsNumber(RepoValueKind p_Kind) {
    return p_Kind == RepoValueKind::Float32 || p_Kind == RepoValueKind::Float64;
}

bool IsDartGun(const RepoRarityEntry& p_Entry) {
    return fr::IsPistolIcon(p_Entry.icon) && fr::IsDartGun(p_Entry.audioExotic, p_Entry.title);
}

// A dart gun can't kill, so it never meets "Unsilenced Pistol Kill - Epic".
bool IsUnsilencedPistol(const RepoRarityEntry& p_Entry) {
    return fr::IsUnsilencedPistol(p_Entry.icon, p_Entry.audioHead, p_Entry.audioTail, p_Entry.audioExotic,
                                  p_Entry.title);
}

std::string CapacityText(double p_Value) {
    return fmt::format("{:g}", p_Value);
}

} // namespace

void WeaponRarityShuffle::Configure(bool p_On, fr::KeepRange p_Keep, bool p_Capacity) {
    m_On = p_On;
    m_Keep = fr::SanitizeKeepRange(p_Keep.min, p_Keep.max);
    m_Capacity = p_Capacity;
}

bool WeaponRarityShuffle::Scan(std::vector<RepoRarityEntry>& p_Entries, const char* p_When) {
    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        LogTo(fr::LogTopic::Weapons, "weapon rarity: {}: pro.repo: {}; nothing changed", p_When, s_Why);
        return false;
    }

    size_t s_Overflow = 0;
    size_t s_Scanned = 0;

    if (const uint32_t s_Code = m_Repo.ScanRarityItems(p_Entries, &s_Overflow, &s_Scanned)) {
        LogTo(fr::LogTopic::Weapons,
              "weapon rarity: {}: the pro.repo scan faulted (exception 0x{:08X}) after {} entries; nothing changed",
              p_When, s_Code, s_Scanned);
        return false;
    }

    if (s_Overflow)
        LogTo(fr::LogTopic::Weapons, "weapon rarity: {}: {} entries with a rarity over the {} limit were not scanned",
              p_When, s_Overflow, ProRepository::kMaxItemEntries);

    return true;
}

void WeaponRarityShuffle::TrackVanilla(const std::vector<RepoRarityEntry>& p_Entries) {
    for (const auto& s_Entry : p_Entries) {
        if (!IsNumber(s_Entry.rarityKind))
            continue;

        const bool s_HasCapacity = IsNumber(s_Entry.capacityKind);
        auto s_It = m_Vanilla.find(s_Entry.guid);

        if (s_It == m_Vanilla.end()) {
            Values s_Values;
            s_Values.rarity = s_Entry.rarity;
            s_Values.hasCapacity = s_HasCapacity;
            s_Values.capacity = s_HasCapacity ? s_Entry.capacity : 0.0;
            m_Vanilla.emplace(s_Entry.guid, s_Values);
            continue;
        }

        Values& s_Values = s_It->second;

        if (s_Entry.rarity != s_Values.rarity &&
            (!s_Values.writtenRarity || s_Entry.rarity != *s_Values.writtenRarity)) {
            LogTo(fr::LogTopic::Weapons,
                  "weapon rarity: {} \"{}\": rarity changed outside this mod ({} -> {}); taken as its vanilla value",
                  fr::FormatGuid(s_Entry.guid), s_Entry.title, s_Values.rarity, s_Entry.rarity);
            s_Values.rarity = s_Entry.rarity;
            s_Values.writtenRarity.reset();
        }

        if (s_HasCapacity && s_Values.hasCapacity && s_Entry.capacity != s_Values.capacity &&
            (!s_Values.writtenCapacity || s_Entry.capacity != *s_Values.writtenCapacity)) {
            LogTo(fr::LogTopic::Weapons,
                  "weapon rarity: {} \"{}\": capacity cost changed outside this mod ({} -> {}); taken as its vanilla "
                  "value",
                  fr::FormatGuid(s_Entry.guid), s_Entry.title, s_Values.capacity, s_Entry.capacity);
            s_Values.capacity = s_Entry.capacity;
            s_Values.writtenCapacity.reset();
        }
    }
}

// Writes back the vanilla value of every entry still holding what this wrote.
size_t WeaponRarityShuffle::RestoreFrom(const std::vector<RepoRarityEntry>& p_Entries, size_t* p_Failed) {
    size_t s_Restored = 0;

    for (const auto& s_Entry : p_Entries) {
        auto s_It = m_Vanilla.find(s_Entry.guid);

        if (s_It == m_Vanilla.end())
            continue;

        Values& s_Values = s_It->second;

        if (s_Values.writtenRarity && IsNumber(s_Entry.rarityKind)) {
            if (s_Entry.rarity == *s_Values.writtenRarity && s_Entry.rarity != s_Values.rarity) {
                if (m_Repo.WriteValue(s_Entry.rarityData, s_Entry.rarityKind, s_Values.rarity))
                    ++*p_Failed;
                else
                    ++s_Restored;
            }

            s_Values.writtenRarity.reset();
        }

        if (s_Values.writtenCapacity && IsNumber(s_Entry.capacityKind)) {
            if (s_Entry.capacity == *s_Values.writtenCapacity && s_Entry.capacity != s_Values.capacity) {
                if (m_Repo.WriteValue(s_Entry.capacityData, s_Entry.capacityKind, s_Values.capacity))
                    ++*p_Failed;
                else
                    ++s_Restored;
            }

            s_Values.writtenCapacity.reset();
        }
    }

    return s_Restored;
}

void WeaponRarityShuffle::Restore(const char* p_When) noexcept {
    try {
        bool s_Any = false;

        for (const auto& [s_Guid, s_Values] : m_Vanilla)
            s_Any = s_Any || s_Values.writtenRarity || s_Values.writtenCapacity;

        if (!s_Any)
            return;

        std::vector<RepoRarityEntry> s_Entries;

        if (!Scan(s_Entries, p_When))
            return;

        size_t s_Failed = 0;
        const size_t s_Restored = RestoreFrom(s_Entries, &s_Failed);
        m_LoggedSeed.reset();
        LogTo(fr::LogTopic::Weapons, "weapon rarity: {}: {} values put back to vanilla{}", p_When, s_Restored,
              s_Failed ? fmt::format(", {} writes faulted", s_Failed) : std::string());
    }
    catch (const std::exception& e) {
        Log("weapon rarity: {}: restore failed: {}", p_When, e.what());
    }
    catch (...) {
        Log("weapon rarity: {}: restore failed: unknown exception", p_When);
    }
}

void WeaponRarityShuffle::Apply(std::optional<int32_t> p_Seed, const char* p_When) noexcept {
    try {
        if (!m_On) {
            // Turned off in the overlay mid-session: vanilla again (nothing when nothing was written).
            Restore(p_When);
            return;
        }

        const auto s_Start = std::chrono::steady_clock::now();
        std::vector<RepoRarityEntry> s_Entries;

        if (!Scan(s_Entries, p_When))
            return;

        TrackVanilla(s_Entries);

        if (!p_Seed || *p_Seed == 0) {
            size_t s_Failed = 0;
            const size_t s_Restored = RestoreFrom(s_Entries, &s_Failed);
            m_LoggedSeed.reset();
            LogTo(fr::LogTopic::Weapons,
                  "weapon rarity: {}: campaign seed unknown; rarities stay vanilla ({} values put back{})", p_When,
                  s_Restored, s_Failed ? fmt::format(", {} writes faulted", s_Failed) : std::string());
            return;
        }

        const int32_t s_Seed = *p_Seed;

        // The firearms with a vanilla rarity of 1-4, and their entries.
        std::vector<fr::RarityWeapon> s_Weapons;
        std::vector<const RepoRarityEntry*> s_WeaponEntries;

        for (const auto& s_Entry : s_Entries) {
            if (!IsNumber(s_Entry.rarityKind) || !fr::IsFirearmIcon(s_Entry.icon))
                continue;

            const auto s_It = m_Vanilla.find(s_Entry.guid);

            if (s_It == m_Vanilla.end())
                continue;

            const auto s_Rarity = fr::RarityFromValue(s_It->second.rarity);

            if (!s_Rarity)
                continue;

            s_Weapons.push_back({s_Entry.guid, *s_Rarity, IsUnsilencedPistol(s_Entry)});
            s_WeaponEntries.push_back(&s_Entry);
        }

        const fr::RarityPlan s_Plan = fr::PlanWeaponRarity(s_Weapons, s_Seed, m_Keep);
        const bool s_LogPlan = m_LoggedSeed != s_Seed;
        size_t s_Written = 0;
        size_t s_Held = 0;
        size_t s_Failed = 0;
        std::vector<int32_t> s_Before;
        std::vector<int32_t> s_After;

        for (size_t i = 0; i < s_Weapons.size(); ++i) {
            const RepoRarityEntry& s_Entry = *s_WeaponEntries[i];
            Values& s_Values = m_Vanilla[s_Entry.guid];
            const fr::RarityChange& s_Change = s_Plan.changes[i];
            const int32_t s_Old = s_Weapons[i].rarity;
            s_Before.push_back(s_Old);
            s_After.push_back(s_Change.rarity);

            const double s_Rarity = static_cast<double>(s_Change.rarity);
            const bool s_CanCapacity = s_Values.hasCapacity && IsNumber(s_Entry.capacityKind);
            const double s_Capacity = s_CanCapacity && m_Capacity
                                          ? fr::CapacityAfter(s_Values.capacity, s_Old, s_Change.rarity)
                                          : s_Values.capacity;

            const auto s_WriteOne = [&](void* p_Data, RepoValueKind p_Kind, double p_Current, double p_Target,
                                        double p_Vanilla, std::optional<double>& p_WrittenSlot) {
                if (p_Current == p_Target) {
                    ++s_Held;
                    p_WrittenSlot = p_Target != p_Vanilla ? std::optional<double>(p_Target) : std::nullopt;
                    return;
                }

                if (m_Repo.WriteValue(p_Data, p_Kind, p_Target)) {
                    ++s_Failed;
                    return;
                }

                ++s_Written;
                p_WrittenSlot = p_Target != p_Vanilla ? std::optional<double>(p_Target) : std::nullopt;
            };

            s_WriteOne(s_Entry.rarityData, s_Entry.rarityKind, s_Entry.rarity, s_Rarity, s_Values.rarity,
                       s_Values.writtenRarity);

            if (s_CanCapacity)
                s_WriteOne(s_Entry.capacityData, s_Entry.capacityKind, s_Entry.capacity, s_Capacity, s_Values.capacity,
                           s_Values.writtenCapacity);

            if (!s_LogPlan)
                continue;

            std::string s_Notes;

            if (s_Change.kept)
                s_Notes += ", kept";

            if (s_Change.epicFix)
                s_Notes += s_Plan.epicPistol == i ? ", EPIC PISTOL FIX" : ", epic pistol fix partner";

            if (s_Weapons[i].unsilencedPistol)
                s_Notes += ", unsilenced pistol";
            else if (IsDartGun(s_Entry))
                s_Notes += ", dart gun";

            LogTo(fr::LogTopic::Weapons, "weapon rarity: {} \"{}\" [{}] {} -> {}{}; capacity {}{}",
                  fr::FormatGuid(s_Entry.guid), s_Entry.title, s_Entry.icon, fr::RarityName(s_Old),
                  fr::RarityName(s_Change.rarity), s_Notes,
                  s_CanCapacity ? CapacityText(s_Values.capacity) : std::string("none"),
                  s_CanCapacity && s_Capacity != s_Values.capacity ? fmt::format(" -> {}", CapacityText(s_Capacity))
                                                                   : std::string());
        }

        // Entries that are no longer weapons of the plan (none expected) keep
        // whatever this wrote; nothing else in m_Vanilla is touched.

        if (s_LogPlan) {
            m_LoggedSeed = s_Seed;
            LogTo(
                fr::LogTopic::Weapons,
                "weapon rarity: {}: campaign seed {}: {} firearms with a rarity; keep share {:.3f}: {} kept, {} in the "
                "cycle, {} changed; before {}; after {}; epic pistol rule: {}{}; capacity {}",
                p_When, s_Seed, s_Weapons.size(), s_Plan.keepShare, s_Plan.keepCount, s_Plan.moverCount,
                s_Plan.changedCount, fr::DescribeRarityCounts(fr::CountRarities(s_Before)),
                fr::DescribeRarityCounts(fr::CountRarities(s_After)), fr::EpicFixName(s_Plan.epic),
                s_Plan.epicPistol ? fmt::format(" ({})", fr::FormatGuid(s_Weapons[*s_Plan.epicPistol].id))
                                  : std::string(),
                m_Capacity ? "follows rarity" : "vanilla");
        }

        LogTo(fr::LogTopic::Weapons,
              "weapon rarity: {}: campaign seed {}: {} values written, {} already held{}, {:.1f} ms", p_When, s_Seed,
              s_Written, s_Held, s_Failed ? fmt::format(", {} writes FAULTED", s_Failed) : std::string(),
              MsSince(s_Start));
    }
    catch (const std::exception& e) {
        Log("weapon rarity: {}: failed: {}", p_When, e.what());
    }
    catch (...) {
        Log("weapon rarity: {}: failed: unknown exception", p_When);
    }
}
