#include "BestFit.h"

namespace memsim {

std::optional<Size> BestFit::selectBlock(const std::vector<MemoryBlock>& blocks,
                                         Size requestSize) const {
    if (requestSize == 0) {
        return std::nullopt;
    }
    std::optional<Size> best;
    for (Size i = 0; i < blocks.size(); ++i) {
        // Strict '<' keeps the earliest (lowest-address) block among equals.
        if (isSuitable(blocks[i], requestSize) &&
            (!best.has_value() || blocks[i].size < blocks[*best].size)) {
            best = i;
        }
    }
    return best;
}

std::string BestFit::name() const { return "Best Fit"; }

}  // namespace memsim
