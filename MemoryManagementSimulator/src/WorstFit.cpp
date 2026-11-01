#include "WorstFit.h"

namespace memsim {

std::optional<Size> WorstFit::selectBlock(const std::vector<MemoryBlock>& blocks,
                                          Size requestSize) const {
    if (requestSize == 0) {
        return std::nullopt;
    }
    std::optional<Size> worst;
    for (Size i = 0; i < blocks.size(); ++i) {
        // Strict '>' keeps the earliest (lowest-address) block among equals.
        if (isSuitable(blocks[i], requestSize) &&
            (!worst.has_value() || blocks[i].size > blocks[*worst].size)) {
            worst = i;
        }
    }
    return worst;
}

std::string WorstFit::name() const { return "Worst Fit"; }

}  // namespace memsim
