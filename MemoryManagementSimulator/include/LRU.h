#pragma once

#include <list>
#include <unordered_map>

#include "PageReplacement.h"

namespace memsim {

// Least Recently Used: evicts the resident page whose last reference is oldest.
//
// Doubly linked list ordered by recency (front = most recent, back = least recent)
// plus a hash map from page to its list node. A hit moves the node to the front
// with splice (no allocation, iterators stay valid). Lookup, touch, insert and
// evict are O(1) average.
class LRU final : public PageReplacementAlgorithm {
public:
    std::string name() const override;

protected:
    void onReset(const std::vector<PageId>& references) override;
    void onHit(PageId page, Size position) override;
    void onInsert(PageId page, Size position) override;
    void onEvict(PageId page) override;
    PageId selectVictim(Size position) const override;

private:
    std::list<PageId> recency_;
    std::unordered_map<PageId, std::list<PageId>::iterator> node_;
};

}  // namespace memsim
