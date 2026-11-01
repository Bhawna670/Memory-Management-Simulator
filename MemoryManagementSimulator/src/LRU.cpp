#include "LRU.h"

namespace memsim {

std::string LRU::name() const { return "LRU"; }

void LRU::onReset(const std::vector<PageId>&) {
    recency_.clear();
    node_.clear();
}

void LRU::onHit(PageId page, Size) {
    // splice relinks the existing node: O(1), and the stored iterator remains valid.
    recency_.splice(recency_.begin(), recency_, node_.at(page));
}

void LRU::onInsert(PageId page, Size) {
    recency_.push_front(page);
    node_[page] = recency_.begin();
}

void LRU::onEvict(PageId page) {
    const auto it = node_.find(page);
    recency_.erase(it->second);
    node_.erase(it);
}

PageId LRU::selectVictim(Size) const { return recency_.back(); }

}  // namespace memsim
