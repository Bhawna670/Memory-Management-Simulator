#pragma once

#include <limits>
#include <unordered_map>
#include <vector>

#include "PageReplacement.h"

namespace memsim {

// Optimal (Belady's MIN): evicts the resident page whose next use is farthest in
// the future, or that is never used again. Offline benchmark: it needs the whole
// reference string, which it receives in reset(), so it cannot be implemented in
// a real OS.
//
// Future analysis is vector-based: reset() builds nextOccurrence_[i], the index of
// the next reference to the same page after position i (kNever if none), in one
// backward O(n) pass. Each resident page remembers its next use, so choosing a
// victim is an O(frames) scan instead of rescanning the reference string.
//
// Ties (several pages never used again) go to the lowest frame slot, so results
// are deterministic.
//
// access() must be fed exactly the string given to reset(); a mismatch throws
// std::logic_error.
class Optimal final : public PageReplacementAlgorithm {
public:
    std::string name() const override;

protected:
    void onReset(const std::vector<PageId>& references) override;
    void onHit(PageId page, Size position) override;
    void onInsert(PageId page, Size position) override;
    void onEvict(PageId page) override;
    PageId selectVictim(Size position) const override;

private:
    static constexpr Size kNever = std::numeric_limits<Size>::max();

    void recordUse(PageId page, Size position);

    std::vector<PageId> references_;
    std::vector<Size> nextOccurrence_;
    std::unordered_map<PageId, Size> nextUse_;  // resident page -> index of its next reference
};

}  // namespace memsim
