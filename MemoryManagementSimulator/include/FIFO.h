#pragma once

#include <queue>

#include "PageReplacement.h"

namespace memsim {

// First In First Out: evicts the page that has been resident longest.
// Hits do not change the order. All hooks are O(1).
class FIFO final : public PageReplacementAlgorithm {
public:
    std::string name() const override;

protected:
    void onReset(const std::vector<PageId>& references) override;
    void onHit(PageId page, Size position) override;
    void onInsert(PageId page, Size position) override;
    void onEvict(PageId page) override;
    PageId selectVictim(Size position) const override;

private:
    std::queue<PageId> arrivalOrder_;  // front = oldest resident page
};

}  // namespace memsim
