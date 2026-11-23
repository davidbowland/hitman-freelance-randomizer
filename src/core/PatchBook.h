#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace fr {

// Identifies one patched value: an entity (pointer, 0) or a repository entry
// (GUID high, GUID low).
struct PatchKey {
    uint64_t a = 0;
    uint64_t b = 0;

    auto operator<=>(const PatchKey&) const = default;
};

// One value this mod changed and has not restored yet.
struct Patch {
    int64_t tag;     // what the value is (the EDifficultyParameter), checked on reuse
    double original; // the value before this mod first wrote it
    double written;  // the value this mod wrote last
};

enum class RestoreDecision {
    Restore,          // still holds what was written: write the original back
    AlreadyOriginal,  // already back at the original: nothing to do
    ChangedElsewhere, // something else changed it since: leave it alone
};

RestoreDecision DecideRestore(double current, const Patch& patch);

// Outstanding patches, so they can be restored (on scene clear, or before a
// new scene is patched) and so a value still holding what this mod wrote is
// never rolled from again (no stacking).
class PatchBook {
public:
    // Records a write. When the key already has an outstanding patch with the
    // same tag, its original is kept (only the written value changes).
    void Record(const PatchKey& key, int64_t tag, double original, double written);

    // The value to roll from: the original when current still equals the last
    // written value for the same tag, otherwise current.
    double BaseFor(const PatchKey& key, int64_t tag, double current) const;

    // True when BaseFor would return the stored original rather than current.
    bool WouldRestoreBase(const PatchKey& key, int64_t tag, double current) const;

    const Patch* Find(const PatchKey& key) const;
    void Forget(const PatchKey& key);
    void Clear();
    bool Empty() const;
    size_t Size() const;

    // Every outstanding patch, in key order.
    std::vector<std::pair<PatchKey, Patch>> Entries() const;

private:
    std::map<PatchKey, Patch> m_Patches;
};

// The value a repository entry gets for a multiplier: base * multiplier,
// rounded to float when the stored value is a float32.
double ScaleRepoValue(double base, double multiplier, bool isFloat32);

// True when a float32 entity value equals a repository value rounded to float
// (within one float step either way). Never true for NaN or infinity.
bool EntityMatchesRepo(float entity, double repo);

} // namespace fr
