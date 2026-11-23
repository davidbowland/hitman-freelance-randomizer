#include "BriefingGraphs.h"

#include "BlueprintRead.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"

#include <Glacier/ZEntity.h>

#include <atomic>
#include <mutex>
#include <utility>

namespace briefing_graphs {

namespace {

// A Hokkaido variant load copies its scene bricks' and the variant's
// blueprints; a large scene brick has some 10,000 pin connections.
constexpr size_t kMaxGraphs = 256;
constexpr size_t kMaxLinks = 600'000;

std::atomic<bool> g_Capture {false};
std::atomic<size_t> g_Seen {0};
std::atomic<size_t> g_Copied {0};
std::atomic<size_t> g_Faults {0};

std::mutex& CacheMutex() {
    static std::mutex s_Mutex;
    return s_Mutex;
}

fr::BriefingGraphCache& Cache() {
    static fr::BriefingGraphCache s_Cache(kMaxGraphs, kMaxLinks);
    return s_Cache;
}

// Raw-pointer steps follow the guard pattern in Guard.h.

__declspec(noinline) void ResendOf(const Resend* p_Link, const ZObjectRef* p_Data) {
    const ZEntityRef s_Target(p_Link->entity);

    if (p_Link->output)
        s_Target.SignalOutputPin(p_Link->pin, *p_Data);
    else
        s_Target.SignalInputPin(p_Link->pin, *p_Data);
}

__declspec(noinline) DWORD SafeResend(const Resend* p_Link, const ZObjectRef* p_Data) {
    DWORD s_Code = 0;

    __try {
        ResendOf(p_Link, p_Data);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

void SetCapture(bool p_On) noexcept {
    g_Capture.store(p_On, std::memory_order_relaxed);
}

void Clear() {
    std::lock_guard s_Lock(CacheMutex());
    Cache() = fr::BriefingGraphCache(kMaxGraphs, kMaxLinks);
}

void OnBlueprintBuilt(const void* p_Factory, const STemplateEntityBlueprint* p_Blueprint) noexcept {
    if (!g_Capture.load(std::memory_order_relaxed) || !p_Factory || !p_Blueprint)
        return;

    try {
        g_Seen.fetch_add(1, std::memory_order_relaxed);
        bool s_EventOut = false;
        bool s_TriggerIn = false;
        bool s_StoryIn = false;
        bool s_ExitIn = false;
        bool s_HintIn = false;

        if (ScanBriefingPins(p_Blueprint, &s_EventOut, &s_TriggerIn, &s_StoryIn, &s_ExitIn, &s_HintIn)) {
            g_Faults.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // A briefing's blueprint, one that unlocks or tracks a mission story (core/MissionStories.h), a Freelancer
        // exit template (core/VariantExits.h; a vanilla exit template's is a briefing's by its pins), or one that
        // queues a hint (a variant's commentary, with Sloth's vitality next to it: core/VariantComments.h).
        if (!fr::MayHoldBriefing(s_EventOut, s_TriggerIn) && !s_StoryIn && !s_ExitIn && !s_HintIn)
            return;

        std::string s_Why;
        std::optional<fr::BriefingGraph> s_Graph = CopyBriefingGraph(p_Blueprint, &s_Why);

        if (!s_Graph) {
            g_Faults.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        std::lock_guard s_Lock(CacheMutex());
        Cache().Put(reinterpret_cast<uintptr_t>(p_Factory), std::move(*s_Graph));
        g_Copied.fetch_add(1, std::memory_order_relaxed);
    }
    catch (...) {
        g_Faults.fetch_add(1, std::memory_order_relaxed);
    }
}

std::optional<fr::BriefingGraph> Captured(const void* p_Factory) {
    std::lock_guard s_Lock(CacheMutex());
    const fr::BriefingGraph* s_Graph = Cache().Get(reinterpret_cast<uintptr_t>(p_Factory));
    return s_Graph ? std::optional<fr::BriefingGraph>(*s_Graph) : std::nullopt;
}

std::optional<fr::BriefingGraph> GraphOf(ZEntityBlueprintFactoryBase* p_Factory,
                                         const std::vector<std::pair<int32_t, uint64_t>>& p_Seen, std::string* p_Why) {
    std::string s_Resident;

    if (const STemplateEntityBlueprint* s_Blueprint = TemplateBlueprintOfFactory(p_Factory, &s_Resident)) {
        if (std::optional<fr::BriefingGraph> s_Graph = CopyBriefingGraph(s_Blueprint, &s_Resident))
            return s_Graph;
    }

    std::optional<fr::BriefingGraph> s_Graph = Captured(p_Factory);

    if (!s_Graph) {
        *p_Why = fmt::format("{}, and it was not copied while the scene loaded", s_Resident);
        return std::nullopt;
    }

    int64_t s_Count = 0;

    if (const uint32_t s_Code = ReadSubEntityCount(p_Factory, &s_Count)) {
        *p_Why = fmt::format("reading the factory's sub-entity count: {}", FaultText(s_Code));
        return std::nullopt;
    }

    if (!fr::BriefingGraphMatches(*s_Graph, s_Count > 0 ? static_cast<size_t>(s_Count) : 0, p_Seen)) {
        *p_Why = fmt::format("the copy made while the scene loaded does not match it ({} sub-entities, the factory "
                             "has {})",
                             s_Graph->ids.size(), s_Count);
        return std::nullopt;
    }

    return s_Graph;
}

std::string SubName(const fr::BriefingGraph& p_Graph, int32_t p_Index) {
    if (p_Index < 0 || static_cast<size_t>(p_Index) >= p_Graph.ids.size())
        return fmt::format("#{}", p_Index);

    const std::string& s_Name = p_Graph.names[static_cast<size_t>(p_Index)];
    return s_Name.empty() ? fmt::format("{:016X}", p_Graph.ids[static_cast<size_t>(p_Index)]) : s_Name;
}

Stats GetStats() {
    std::lock_guard s_Lock(CacheMutex());
    return {g_Seen.load(std::memory_order_relaxed),
            g_Copied.load(std::memory_order_relaxed),
            g_Faults.load(std::memory_order_relaxed),
            Cache().Size(),
            Cache().Links(),
            Cache().Dropped()};
}

ResendResult SendOn(std::span<const Resend> p_Links, ZEntityType** p_From, uint32_t p_FromPin,
                    const ZObjectRef& p_Data) noexcept {
    ResendResult s_Result;

    for (const Resend& s_Link : p_Links) {
        if (!s_Link.entity || (s_Link.output && s_Link.entity == p_From && s_Link.pin == p_FromPin))
            continue; // never back into the same block

        try {
            if (SafeResend(&s_Link, &p_Data))
                ++s_Result.faults;
            else
                ++s_Result.sent;
        }
        catch (...) {
            ++s_Result.faults;
        }
    }

    return s_Result;
}

} // namespace briefing_graphs
