#include "MemoryBlock.h"

#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace memsim {

std::string toString(BlockState state) {
    return state == BlockState::Free ? "Free" : "Allocated";
}

MemoryBlock MemoryBlock::makeFree(Size start, Size size) {
    MemoryBlock block;
    block.start = start;
    block.size = size;
    return block;
}

MemoryBlock MemoryBlock::makeAllocated(Size start, Size size, ProcessId owner,
                                       Size requestedSize) {
    MemoryBlock block;
    block.start = start;
    block.size = size;
    block.state = BlockState::Allocated;
    block.owner = owner;
    block.requestedSize = requestedSize;
    return block;
}

Size MemoryBlock::internalFragmentation() const noexcept {
    if (state != BlockState::Allocated || requestedSize > size) {
        return 0;
    }
    return size - requestedSize;
}

std::vector<MemoryBlock> makeInitialBlocks(const std::vector<Size>& blockSizes) {
    if (blockSizes.empty()) {
        throw std::invalid_argument("at least one memory block is required");
    }
    std::vector<MemoryBlock> blocks;
    blocks.reserve(blockSizes.size());
    Size cursor = 0;
    for (Size i = 0; i < blockSizes.size(); ++i) {
        const Size size = blockSizes[i];
        if (size == 0) {
            throw std::invalid_argument("block " + std::to_string(i) + " has size 0");
        }
        if (size > std::numeric_limits<Size>::max() - cursor) {
            throw std::invalid_argument("total memory size overflows");
        }
        blocks.push_back(MemoryBlock::makeFree(cursor, size));
        cursor += size;
    }
    return blocks;
}

BlockListValidation validateBlockList(const std::vector<MemoryBlock>& blocks, Size expectedTotal) {
    const auto fail = [](Size index, const std::string& why) {
        return BlockListValidation{false, "block " + std::to_string(index) + ": " + why};
    };

    Size cursor = 0;
    std::unordered_set<ProcessId> owners;

    for (Size i = 0; i < blocks.size(); ++i) {
        const MemoryBlock& b = blocks[i];

        if (b.size == 0) {
            return fail(i, "size is 0");
        }
        if (b.start != cursor) {
            return fail(i, "start " + std::to_string(b.start) + " but expected " +
                               std::to_string(cursor) + " (gap or overlap)");
        }
        if (b.size > std::numeric_limits<Size>::max() - cursor) {
            return fail(i, "end address overflows");
        }

        if (b.state == BlockState::Free) {
            if (b.owner.has_value()) {
                return fail(i, "free block has an owner");
            }
            if (b.requestedSize != 0) {
                return fail(i, "free block has a nonzero requested size");
            }
        } else {
            if (!b.owner.has_value()) {
                return fail(i, "allocated block has no owner");
            }
            if (b.requestedSize == 0) {
                return fail(i, "allocated block has requested size 0");
            }
            if (b.requestedSize > b.size) {
                return fail(i, "requested size exceeds block size");
            }
            if (!owners.insert(*b.owner).second) {
                return fail(i, "process " + std::to_string(*b.owner) + " owns more than one block");
            }
        }
        cursor += b.size;
    }

    if (cursor != expectedTotal) {
        return {false, "blocks cover " + std::to_string(cursor) + " units but total memory is " +
                           std::to_string(expectedTotal)};
    }
    return {};
}

}  // namespace memsim
