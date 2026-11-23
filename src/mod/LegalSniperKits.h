#pragma once

// Legal sniper rifles (legal_sniper_rifles, on by default; pure part in
// core/LegalSniper.h). In Freelancer
// scenes, 2 s of game time after eLoading_ScenePlaying and every 15 s after,
// the outfit kits (ZGlobalOutfitKit) of the activated and spawned actors are
// read: m_bWeaponsAllowed and the four weapon lists, each keyword entity's
// keyword (through the game's ZGameKeywordManager; logged once per scene
// with how it was found) and each item key's pro.repo item. Each kit is logged
// once per scene with its verdict and the lists' engine types; a kit that
// allows visible firearms but excepts sniper rifles gets its sniper
// exceptions removed in place (no allocation: the kept entries first, the
// array's count shrunk); one whose only doubt is an exception the mod cannot
// read is left alone and logged as unknown. At the scene's clear (and on
// mod unload) every edited list whose header still holds what was written
// gets its vanilla order and count back; one the game changed is left alone.
// Every engine read and write is guarded; the entry points catch everything.

#include "Guid.h"
#include "LegalSniper.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class ProRepository;

class LegalSniperKits {
public:
    explicit LegalSniperKits(ProRepository& p_Repo) : m_Repo(p_Repo) {}

    // At config load.
    void Configure(bool p_On) { m_On = p_On; }

    // Game thread, from the SetLoadingStage detour: arms the first pass at
    // eLoading_ScenePlaying of a Freelancer scene.
    void OnLoadingStage(ESceneLoadingStage p_Stage, bool p_Freelancer) noexcept;

    // Game thread, every play-mode frame (game seconds).
    void Tick(double p_Seconds) noexcept;

    // Game thread, at a scene clear (before the kits are destroyed) and on
    // mod unload: puts every edited list back, then forgets the scene.
    void Restore(const char* p_When) noexcept;

private:
    // One exception list this edited.
    struct Edit {
        std::string kit;
        std::string list;
        uintptr_t address = 0;             // the TArray itself
        fr::RawArrayHeader vanilla;        // as read before the edit
        fr::RawArrayHeader written;        // as left by the edit
        uintptr_t elements = 0;            // where the elements are (begin, or the array itself when inline)
        std::vector<uint8_t> vanillaBytes; // the elements in their vanilla order (empty: order unchanged)
    };

    void Pass(const char* p_When);
    bool EnsureItems();

    ProRepository& m_Repo;
    bool m_On = true;

    // Per scene.
    bool m_Armed = false;
    double m_Clock = 0.0;    // game seconds since eLoading_ScenePlaying
    double m_NextPass = 0.0; // game seconds
    size_t m_Passes = 0;
    std::set<ZEntityType**> m_SeenKits;

    // A keyword entity's keyword(s), as read (see ReadKeyword in the .cpp).
    struct Keyword {
        std::vector<std::string> texts; // empty: unreadable
        std::string how;                // where the texts came from, or why none
    };
    std::map<ZEntityType**, Keyword> m_Keywords;

    // The game's keyword ids (ZGameKeywordManager::m_KeywordIDtoStringMap),
    // read when a keyword entity first needs them, and at most once per pass
    // after that: again when an earlier pass's read failed, or when a lookup
    // in an earlier pass's read finds nothing.
    struct KeywordBook {
        bool read = false;
        size_t pass = 0; // m_Passes when read
        std::string why; // empty when read
        uint32_t counter = 0;
        size_t size = 0;
        std::map<int32_t, std::string> names;
        std::vector<int32_t> sniperIds;
    };
    KeywordBook m_Book;

    const KeywordBook& Book(bool p_Missed = false);
    const Keyword& ReadKeyword(ZEntityType** p_Entity);

    std::vector<Edit> m_Edits;
    std::array<size_t, fr::kKitSniperVerdicts> m_Verdicts {}; // fr::KitSniper counts

    // Per session: pro.repo items (icon, ItemType, title) by id, for the item keys.
    struct Item {
        std::string icon;
        std::string itemType;
        std::string title;
    };
    bool m_ItemsBuilt = false;
    std::map<fr::Guid, Item> m_Items;
};
