#pragma once

// Supplier steering (supplier_steer; pure part in core/StockSteerMath.h).
// Weights only: no choice list and no choice condition is ever written, so
// the game's own rule (only guns you do not own and are entitled to) still
// decides what can be offered.
//
// The supplier walk (SupplierWalk.h) hands over its options and selectors
// (Prepare), its play time (Tick) and the offers it sees (OnOffer).
// - Steering: the five gun selectors (m_Seed fr::kCampaignSeedEntityId). At
//   fr::kStockSteerAt s of play each slot's eligible guns (owned / entitled
//   flags, fr::StockEligible) get their m_fB lowered so their weights come
//   out equal (fr::PlanSupplierSteer); until the slot rolls the values are
//   checked every fr::kStockSteerCheckEvery s, and the weight each eligible
//   gun's math outputs at the roll is rewritten to the planned one on its
//   way to the choice. fr::kStockRestoreAfter s after the slot's last pick
//   signal the original m_fB and the game's weights are written back.
// - Gear (supplier_steer_gear): the options no selector draws, less the
//   firearms and those without a weight math, are one pseudo-slot planned
//   the same way; it is restored at fr::GearRestoreClock's moment.
// - Force pick (supplier_force_pick; core/SupplierForcePick.h): after the gun
//   selectors' burst of pick signals (fr::GunPickBurst), each gun selector
//   that stayed silent, never picked this scene and has an eligible gun gets
//   its Pick input signaled (outside the lock: its answer comes back through
//   OnOutputPin), unless the supplier filled an alternative stand during the
//   burst (a forced pick could put a second item on a stand).
// Lines start with "stock steer supplier" or "stock force pick" (log_supplier_crates).
// Every engine read and write is guarded; the entry points catch everything.

#include "PinWatchTable.h"
#include "StockSteerMath.h"
#include "SupplierForcePick.h"

#include <Glacier/ZEntity.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ZObjectRef;

class StockSteer {
public:
    // One option as the supplier walk read it (addresses of raw float32 / bool storage; 0 when not found).
    struct OptionInfo {
        std::string name;        // the item's title
        std::string category;    // InventoryCategoryIcon
        std::string entitlement; // Evergreen_UnlockEntitlement (empty: base game)
        bool firearm = false;
        bool hasRarity = false;
        int32_t rarity = 0;
        ZEntityType** math = nullptr;        // its weight math (ZMathMultiplyDivide)
        uintptr_t weightAddress = 0;         // the choice's m_Weight
        uintptr_t faAddress = 0;             // the math's m_fA
        uintptr_t fbAddress = 0;             // the math's m_fB
        uintptr_t conditionAddress = 0;      // the choice condition's m_bValue
        std::vector<uintptr_t> unownedFlags; // fr::kUnownedFlagIds members' m_bValue
        uintptr_t entitledFlag = 0;          // fr::kEntitledFlagId member's m_bValue
    };

    struct SelectorInfo {
        ZEntityType** entity = nullptr;
        uint64_t seedId = 0;
        uintptr_t seedAddress = 0; // its seed's int32 value
        std::string label;
        std::vector<size_t> options; // indices into the options handed over
    };

    // At config load. Nothing runs when supplier and force pick are off;
    // p_Gear only counts with p_Supplier.
    void Configure(bool p_Supplier, bool p_Gear, bool p_ForcePick);

    bool On() const { return m_SupplierOn || m_ForcePickOn; }

    // Any thread: whether the pin hook has anything to do (one relaxed load).
    bool Armed() const { return m_Armed.load(std::memory_order_relaxed); }

    // Game thread, from the supplier walk (scene started in a mission).
    // p_Options is indexed as the walk's options.
    void Prepare(std::string_view p_Scene, std::vector<OptionInfo> p_Options,
                 std::vector<SelectorInfo> p_Selectors) noexcept;

    // Game thread, every play-mode frame of the supplier walk, with its play time.
    void Tick(double p_Play) noexcept;

    // Game thread, at the supplier walk's scene clear: the summary, then the load is forgotten.
    void Clear() noexcept;

    // At mod unload: every value the steering still holds is written back, then the load is forgotten.
    void Restore(const char* p_Why) noexcept;

    // Any thread, from the SignalOutputPin detour, before the engine passes
    // the signal on: selector picks, weight math outputs.
    void OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

    // Any thread, from the supplier walk: an entity activated under option p_Option.
    void OnOffer(size_t p_Option) noexcept;

    // Game thread, from the supplier walk's frame: SupplierVisited came (after the
    // steer, the gear pseudo-slot is restored fr::kGearRestoreAfterVisit s later;
    // before it, right after the steer).
    void OnSupplierVisited() noexcept;

private:
    // Watched entities' values: index << 2 | role (role 0, a cleared slot, is rejected).
    static constexpr uint32_t kRoleSelector = 1;
    static constexpr uint32_t kRoleMath = 2;

    // A steered option (one of a steered selector's).
    struct Steered {
        size_t option = 0;   // index into m_Options
        size_t selector = 0; // index into m_Selectors
        bool eligible = false;
        double fA = 0.0;                  // predicted m_fA
        double fB = 0.0;                  // the game's m_fB, read before any write
        double fBHeld = -1.0;             // the m_fB written (-1: none)
        double target = -1.0;             // the weight held at the roll (-1: none)
        std::optional<double> gameWeight; // the weight the game's math output carried at the roll
        bool offered = false;
    };

    struct Slot {
        SelectorInfo info;
        bool gear = false;           // the gear pseudo-slot (no selector entity)
        std::vector<size_t> steered; // indices into m_Steered
        size_t eligible = 0;
        double weight = 0.0; // the planned common weight
        bool planned = false;
        uint32_t picks = 0;
        double firstPick = -1.0; // play time
        double lastPick = -1.0;
        bool restored = false;
        std::string restoreWhy;      // gear: what set the restore moment
        uint32_t rewrites = 0;       // math outputs rewritten
        uint32_t outputs = 0;        // math outputs seen during the window
        uint32_t rewrittenAgain = 0; // values the game changed back, written again
        std::string offers;
        // Force pick.
        uint32_t burstDone = 0; // pick signals in the current burst
        uint32_t burstFailed = 0;
        bool everDone = false;    // an OnPickDone this scene
        bool forced = false;      // forced in the current burst
        uint32_t forcedVisit = 0; // the burst it was last forced in (fr::GunPickBurst::Visits)
        double forcedAt = -1.0;   // play time of the last forced Pick (-1: never)
        uint32_t forceDone = 0;   // its answers
        uint32_t forceFailed = 0;
        bool forceJudged = false; // its outcome logged
        std::string forceOffers;  // offers under it since the forced Pick
        std::string forceOutcome; // for the summary
    };

    // A slot to force: its index and selector (signaled outside the lock).
    struct ForceTarget {
        size_t slot = 0;
        ZEntityType** entity = nullptr;
    };

    void Steer();
    void Plan(Slot& p_Slot, bool p_First);
    void OnPick(size_t p_Slot, const char* p_What);
    void OnGunSignal(size_t p_Slot, bool p_Done);
    std::vector<ForceTarget> EndBurst();
    void JudgeForced(Slot& p_Slot);
    size_t EligibleGuns(const Slot& p_Slot, std::string* p_Names) const;
    void Restore(Slot& p_Slot);
    void Pin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data);
    bool Eligible(const OptionInfo& p_Option) const;
    std::string HeldText(const Slot& p_Slot) const;
    std::string SeedText(const SelectorInfo& p_Selector) const;
    std::string OptionText(size_t p_Option) const;
    void PrepareGear(const std::vector<bool>& p_InSelector);
    const char* Prefix(const Slot& p_Slot) const;
    void Watch(const void* p_Entity, size_t p_Index, uint32_t p_Role);
    void Forget();

    bool m_SupplierOn = false;
    bool m_GearOn = false;
    bool m_ForcePickOn = false;
    std::atomic<bool> m_Armed {false};
    std::atomic<const void*> m_Float32Type {nullptr};
    fr::PinWatchTable<2048> m_Watch;

    // Under m_Mutex.
    mutable std::mutex m_Mutex;
    std::string m_Scene;
    bool m_Prepared = false;
    bool m_SteerDone = false;
    double m_Play = 0.0;
    double m_NextCheck = 0.0;
    std::vector<OptionInfo> m_Options;
    std::vector<Slot> m_Slots;
    std::vector<Steered> m_Steered;
    std::vector<size_t> m_SteeredOf;   // per option: index into m_Steered, or SIZE_MAX
    std::vector<bool> m_Loose;         // per option: in no selector (gear steering on)
    std::vector<char> m_LooseBlock;    // per option: fr::LooseBlockOf among the options in no selector ('\0': in one)
    std::vector<size_t> m_StandOffers; // alternative-stand options offered in the current burst (force pick)
    size_t m_GearSlot = SIZE_MAX;      // index into m_Slots of the gear pseudo-slot
    fr::GearRestoreClock m_GearClock;
    fr::GunPickBurst m_GunBurst; // the gun selectors' pick signals (force pick)
};
