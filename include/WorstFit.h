#pragma once

#include "PlacementStrategy.h"

namespace memsim {

// Selects the largest suitable free block; among equals, the lowest address. O(n).
class WorstFit final : public PlacementStrategy {
public:
    std::optional<Size> selectBlock(const std::vector<MemoryBlock>& blocks,
                                    Size requestSize) const override;
    std::string name() const override;
};

}  // namespace memsim
