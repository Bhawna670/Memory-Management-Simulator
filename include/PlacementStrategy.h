#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MemoryBlock.h"
#include "Types.h"

namespace memsim {

enum class PlacementAlgorithm { FirstFit, BestFit, WorstFit };

std::string toString(PlacementAlgorithm algorithm);

// Chooses which free block should satisfy a request. Selection only: splitting,
// state changes and accounting belong to MemoryAllocator.
//
// Contract:
//  - `blocks` is address-ordered (as maintained by MemoryAllocator).
//  - A block is suitable iff it is free and block.size >= requestSize.
//  - Returns the index into `blocks`, or nullopt if requestSize == 0 or no
//    block is suitable.
//  - Ties are resolved toward the lowest address, so results are deterministic.
class PlacementStrategy {
public:
    virtual ~PlacementStrategy() = default;

    virtual std::optional<Size> selectBlock(const std::vector<MemoryBlock>& blocks,
                                            Size requestSize) const = 0;
    virtual std::string name() const = 0;

protected:
    static bool isSuitable(const MemoryBlock& block, Size requestSize) noexcept {
        return block.isFree() && block.size >= requestSize;
    }
};

// Throws std::invalid_argument for an out-of-range enum value.
std::unique_ptr<PlacementStrategy> makePlacementStrategy(PlacementAlgorithm algorithm);

}  // namespace memsim
