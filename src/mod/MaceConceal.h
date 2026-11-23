#pragma once

// Concealable mace (concealable_mace, its own option, with or without
// weapon_rarity; see core/ConcealableMace.h). Writes the mace entries' pro.repo "HolsterAbility"
// (eHolsteringAllowed) and "ItemSize" (the size a pocketable melee item has)
// in place, at the same moments as the weapon rarity shuffle (LoadScene of a
// Freelancer scene, a picked campaign, a restart; called just before it), so the item 47 packs or
// finds is built pocketable and comes without an ICA Briefcase, and the
// random loadout planner (which reads ItemSize after the rarity shuffle)
// counts it as a pocket item. The strings are copied with ZString's own
// refcounting: the target text from another pro.repo entry that holds it,
// the vanilla text kept in a ZString this holds until it is put back (a
// non-Freelancer scene, the option off, mod unload).

#include "Guid.h"
#include "Repository.h"

#include <Glacier/ZString.h>

#include <map>
#include <optional>
#include <string>

class MaceConceal {
public:
    explicit MaceConceal(ProRepository& p_Repo) : m_Repo(p_Repo) {}

    // Game thread: makes every mace concealable (logged).
    void Apply(const char* p_When) noexcept;

    // Game thread: puts back every value this wrote that still holds it.
    void Restore(const char* p_When) noexcept;

private:
    struct Key {
        bool tracked = false; // vanilla captured
        ZString vanilla;      // the vanilla ZString (refcount held)
        std::string vanillaText;
        std::optional<std::string> written; // what this wrote, until restored
    };

    struct Mace {
        std::string title;
        Key itemSize;
        Key holster;
    };

    // Captures (or, after a change by someone else, re-captures) the vanilla
    // value of one key; false when the copy faulted.
    bool Track(Key& p_Key, ZString* p_Value, const char* p_Current, const Mace& p_Mace, const char* p_Name);
    bool AnyWritten() const;

    ProRepository& m_Repo;
    std::map<fr::Guid, Mace> m_Maces;
    std::string m_LoggedPlan; // the last plan logged in full (its text), so a repeat logs one line
    bool m_LoggedNone = false;
};
