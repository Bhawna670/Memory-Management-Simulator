#pragma once

#include "PlacementStrategy.h"

namespace memsim {

// Selects the first (lowest-address) suitable free block. O(n).
class FirstFit final : public PlacementStrategy {
public:
    std::optional<Size> selectBlock(const std::vector<MemoryBlock>& blocks,
                                    Size requestSize) const override;
    std::string name() const override;
};

}  // namespace memsim
