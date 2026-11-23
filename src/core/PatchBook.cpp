#include "PatchBook.h"

#include <cmath>
#include <limits>

namespace fr {

RestoreDecision DecideRestore(double current, const Patch& patch) {
    if (current == patch.written && current != patch.original)
        return RestoreDecision::Restore;

    if (current == patch.original)
        return RestoreDecision::AlreadyOriginal;

    return RestoreDecision::ChangedElsewhere;
}

void PatchBook::Record(const PatchKey& key, int64_t tag, double original, double written) {
    const auto it = m_Patches.find(key);

    if (it != m_Patches.end() && it->second.tag == tag) {
        it->second.written = written;
        return;
    }

    m_Patches.insert_or_assign(key, Patch {tag, original, written});
}

bool PatchBook::WouldRestoreBase(const PatchKey& key, int64_t tag, double current) const {
    const auto it = m_Patches.find(key);
    return it != m_Patches.end() && it->second.tag == tag && it->second.written == current;
}

double PatchBook::BaseFor(const PatchKey& key, int64_t tag, double current) const {
    return WouldRestoreBase(key, tag, current) ? m_Patches.at(key).original : current;
}

const Patch* PatchBook::Find(const PatchKey& key) const {
    const auto it = m_Patches.find(key);
    return it == m_Patches.end() ? nullptr : &it->second;
}

void PatchBook::Forget(const PatchKey& key) {
    m_Patches.erase(key);
}

void PatchBook::Clear() {
    m_Patches.clear();
}

bool PatchBook::Empty() const {
    return m_Patches.empty();
}

size_t PatchBook::Size() const {
    return m_Patches.size();
}

std::vector<std::pair<PatchKey, Patch>> PatchBook::Entries() const {
    return {m_Patches.begin(), m_Patches.end()};
}

double ScaleRepoValue(double base, double multiplier, bool isFloat32) {
    const double value = base * multiplier;
    return isFloat32 ? static_cast<double>(static_cast<float>(value)) : value;
}

bool EntityMatchesRepo(float entity, double repo) {
    if (!std::isfinite(entity) || !std::isfinite(repo))
        return false;

    const float expected = static_cast<float>(repo);

    if (entity == expected)
        return true;

    // Within one float step either way.
    return std::nextafter(expected, std::numeric_limits<float>::infinity()) == entity ||
           std::nextafter(expected, -std::numeric_limits<float>::infinity()) == entity;
}

} // namespace fr
