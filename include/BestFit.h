#pragma once

#include "PlacementStrategy.h"

namespace memsim {

// Selects the smallest suitable free block; among equals, the lowest address. O(n).
class BestFit final : public PlacementStrategy {
public:
    std::optional<Size> selectBlock(const std::vector<MemoryBlock>& blocks,
                                    Size requestSize) const override;
    std::string name() const override;
};

}  // namespace memsim
