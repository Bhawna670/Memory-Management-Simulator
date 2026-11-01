#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Types.h"

namespace memsim {

enum class BlockState { Free, Allocated };

std::string toString(BlockState state);

// One contiguous region of memory.
// Invariants (checked by validateBlockList):
//   Free:      owner == nullopt, requestedSize == 0
//   Allocated: owner has a value, 0 < requestedSize <= size
struct MemoryBlock {
    Size start{0};
    Size size{0};
    BlockState state{BlockState::Free};
    std::optional<ProcessId> owner{};
    Size requestedSize{0};

    static MemoryBlock makeFree(Size start, Size size);
    static MemoryBlock makeAllocated(Size start, Size size, ProcessId owner, Size requestedSize);

    bool isFree() const noexcept { return state == BlockState::Free; }
    Size end() const noexcept { return start + size; }  // one past the last address

    // Allocated block size minus requested size. Always 0 for free blocks:
    // unused space in a free block is not internal fragmentation.
    Size internalFragmentation() const noexcept;
};

// Builds a contiguous list of free blocks starting at address 0.
// Throws std::invalid_argument for an empty list, a zero-sized block,
// or a total size that overflows Size.
std::vector<MemoryBlock> makeInitialBlocks(const std::vector<Size>& blockSizes);

struct BlockListValidation {
    bool valid{true};
    std::string message{};  // first violation found; empty when valid
};

// Verifies the block list is a gapless, address-ordered partition of exactly
// expectedTotal units and that every block satisfies its state invariants
// and no process owns more than one block.
BlockListValidation validateBlockList(const std::vector<MemoryBlock>& blocks, Size expectedTotal);

}  // namespace memsim
