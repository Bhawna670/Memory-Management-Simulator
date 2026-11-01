#include "Optimal.h"

#include <optional>
#include <stdexcept>

namespace memsim {

std::string Optimal::name() const { return "Optimal"; }

void Optimal::onReset(const std::vector<PageId>& references) {
    references_ = references;
    nextOccurrence_.assign(references_.size(), kNever);
    nextUse_.clear();

    // Backward pass: lastSeen holds the nearest later index of each page.
    std::unordered_map<PageId, Size> lastSeen;
    for (Size i = references_.size(); i-- > 0;) {
        const auto it = lastSeen.find(references_[i]);
        nextOccurrence_[i] = (it == lastSeen.end()) ? kNever : it->second;
        lastSeen[references_[i]] = i;
    }
}

void Optimal::recordUse(PageId page, Size position) {
    if (position >= references_.size() || references_[position] != page) {
        throw std::logic_error(
            "Optimal requires access() to follow the reference string given to reset()");
    }
    nextUse_[page] = nextOccurrence_[position];
}

void Optimal::onHit(PageId page, Size position) { recordUse(page, position); }

void Optimal::onInsert(PageId page, Size position) { recordUse(page, position); }

void Optimal::onEvict(PageId page) { nextUse_.erase(page); }

PageId Optimal::selectVictim(Size) const {
    std::optional<PageId> victim;
    Size farthest = 0;
    // Slots are scanned in order and '>' is strict, so the lowest slot wins ties.
    for (const auto& slot : frameSlots()) {
        if (!slot.has_value()) {
            continue;
        }
        const Size use = nextUse_.at(*slot);
        if (!victim.has_value() || use > farthest) {
            victim = *slot;
            farthest = use;
        }
    }
    return *victim;
}

}  // namespace memsim
