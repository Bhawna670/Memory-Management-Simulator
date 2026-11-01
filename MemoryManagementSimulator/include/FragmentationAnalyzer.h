#pragma once

#include <vector>

#include "MemoryBlock.h"
#include "Types.h"

namespace memsim {

// Snapshot metrics for a block list.
//   Internal fragmentation: sum over ALLOCATED blocks of (block size - requested size).
//     Unused space inside a free block is never counted as internal fragmentation.
//   External fragmentation indicator: 1 - largestFreeBlock / totalFree, in [0, 1];
//     0.0 when there is no free memory. It measures how scattered free space is,
//     not whether a particular request would fail.
struct FragmentationReport {
    Size totalMemory{0};
    Size allocatedMemory{0};        // sum of allocated block sizes (includes internal fragmentation)
    Size freeMemory{0};
    Size requestedMemory{0};        // sum of requested sizes of allocated blocks
    Size internalFragmentation{0};
    Size freeBlockCount{0};
    Size largestFreeBlock{0};
    double externalFragmentation{0.0};
};

class FragmentationAnalyzer {
public:
    // Memory total is the sum of all block sizes. An empty list gives an all-zero report.
    static FragmentationReport analyze(const std::vector<MemoryBlock>& blocks);

    // Safe for totalFreeMemory == 0 (returns 0.0).
    static double externalFragmentationIndicator(Size largestFreeBlock, Size totalFreeMemory) noexcept;
};

}  // namespace memsim
