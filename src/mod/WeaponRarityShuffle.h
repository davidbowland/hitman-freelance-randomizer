#pragma once

// Weapon rarity shuffle (weapon_rarity, on by default). Freelancer reads a
// weapon's rarity and gear capacity cost from its pro.repo entry
// (Evergreen_Rarity 1..4, Evergreen_CapacityCost); this rewrites them in
// place.
//
// pro.repo stays loaded all session, so this writes the entries in place: at
// LoadScene of every Freelancer scene (safehouse and missions, before the
// scene is built) and when the player picks a new campaign, from the picked
// campaign's seed (fr::PlanWeaponRarity over every firearm with a rarity;
// fr::RarityOnPick / fr::RarityOnMission: campaigns generated but not picked
// change nothing, and the picked campaign's missions, which run with another
// seed of their own, keep the pick's rarities). Each value is
// computed from the vanilla value captured the first time it was seen, never
// from a value this wrote, so loads never compound. Vanilla values are put
// back at a non-Freelancer scene load, when the seed is unknown, and on mod
// unload.
//
// The holsterable mace (concealable_mace) is its own option, applied at the
// same moments by FreelancerRandomizer (MaceConceal), with or without this.

#include "Guid.h"
#include "Repository.h"
#include "WeaponRarity.h"

#include <cstdint>
#include <map>
#include <optional>

class WeaponRarityShuffle {
public:
    explicit WeaponRarityShuffle(ProRepository& p_Repo) : m_Repo(p_Repo) {}

    // At config load.
    void Configure(bool p_On, fr::KeepRange p_Keep, bool p_Capacity);

    // Game thread: a Freelancer scene loads or a campaign was generated. With
    // a known seed (set and not 0) the campaign's plan is written; without
    // one, vanilla values are put back.
    void Apply(std::optional<int32_t> p_Seed, const char* p_When) noexcept;

    // Game thread: puts back every value this wrote that still holds it.
    void Restore(const char* p_When) noexcept;

private:
    struct Values {
        double rarity = 0.0;
        bool hasCapacity = false;
        double capacity = 0.0;
        std::optional<double> writtenRarity; // last value written, until restored
        std::optional<double> writtenCapacity;
    };

    bool Scan(std::vector<RepoRarityEntry>& p_Entries, const char* p_When);
    // Updates m_Vanilla from a scan; a value changed by someone else (not
    // vanilla, not what this wrote) becomes the new vanilla value (logged).
    void TrackVanilla(const std::vector<RepoRarityEntry>& p_Entries);
    size_t RestoreFrom(const std::vector<RepoRarityEntry>& p_Entries, size_t* p_Failed);

    ProRepository& m_Repo;
    bool m_On = false;
    fr::KeepRange m_Keep;
    bool m_Capacity = true;
    std::optional<int32_t> m_LoggedSeed; // the seed whose plan was last logged in full
    std::map<fr::Guid, Values> m_Vanilla;
};
