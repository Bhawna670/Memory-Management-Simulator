#include "FirstFit.h"

namespace memsim {

std::optional<Size> FirstFit::selectBlock(const std::vector<MemoryBlock>& blocks,
                                          Size requestSize) const {
    if (requestSize == 0) {
        return std::nullopt;
    }
    for (Size i = 0; i < blocks.size(); ++i) {
        if (isSuitable(blocks[i], requestSize)) {
            return i;
        }
    }
    return std::nullopt;
}

std::string FirstFit::name() const { return "First Fit"; }

}  // namespace memsim
