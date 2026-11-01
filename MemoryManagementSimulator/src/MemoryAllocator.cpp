#include "MemoryAllocator.h"

#include <algorithm>
#include <stdexcept>

namespace memsim {

std::string toString(AllocationStatus status) {
    switch (status) {
        case AllocationStatus::Success:          return "Success";
        case AllocationStatus::NoSuitableBlock:  return "No suitable free block";
        case AllocationStatus::DuplicateProcess: return "Process already holds an allocation";
        case AllocationStatus::InvalidSize:      return "Process size must be greater than 0";
        case AllocationStatus::InvalidProcessId: return "Process ID must be a positive integer";
    }
    return "Unknown";
}

std::string toString(DeallocationStatus status) {
    switch (status) {
        case DeallocationStatus::Success:         return "Success";
        case DeallocationStatus::ProcessNotFound: return "Process holds no allocation";
    }
    return "Unknown";
}

MemoryAllocator::MemoryAllocator(const std::vector<Size>& blockSizes, PlacementAlgorithm algorithm,
                                 AllocatorConfig config)
    : blocks_(makeInitialBlocks(blockSizes)),
      totalMemory_(0),
      algorithm_(algorithm),
      strategy_(makePlacementStrategy(algorithm)),
      config_(config) {
    if (config_.splitThreshold == 0) {
        throw std::invalid_argument("splitThreshold must be at least 1");
    }
    for (const MemoryBlock& block : blocks_) {
        boundaries_.insert(block.start);
        totalMemory_ += block.size;
    }
}

void MemoryAllocator::setAlgorithm(PlacementAlgorithm algorithm) {
    strategy_ = makePlacementStrategy(algorithm);
    algorithm_ = algorithm;
}

std::optional<Size> MemoryAllocator::findOwner(ProcessId id) const {
    for (Size i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].owner.has_value() && *blocks_[i].owner == id) {
            return i;
        }
    }
    return std::nullopt;
}

Size MemoryAllocator::freeMemory() const {
    Size total = 0;
    for (const MemoryBlock& block : blocks_) {
        if (block.isFree()) {
            total += block.size;
        }
    }
    return total;
}

AllocationResult MemoryAllocator::allocate(ProcessId id, Size size) {
    if (id <= 0) {
        return {AllocationStatus::InvalidProcessId, std::nullopt};
    }
    if (size == 0) {
        return {AllocationStatus::InvalidSize, std::nullopt};
    }
    if (findOwner(id).has_value()) {
        return {AllocationStatus::DuplicateProcess, std::nullopt};
    }

    ++counters_.attempted;
    const std::optional<Size> chosen = strategy_->selectBlock(blocks_, size);
    if (!chosen.has_value()) {
        ++counters_.failed;
        if (freeMemory() >= size) {
            ++counters_.failedDespiteEnoughTotalFree;
        }
        return {AllocationStatus::NoSuitableBlock, std::nullopt};
    }

    const Size index = *chosen;
    const Size start = blocks_[index].start;
    const Size leftover = blocks_[index].size - size;

    blocks_[index].state = BlockState::Allocated;
    blocks_[index].owner = id;
    blocks_[index].requestedSize = size;

    // Split only when the remainder is large enough to be worth tracking;
    // otherwise it remains inside the block as internal fragmentation.
    if (leftover > 0 && leftover >= config_.splitThreshold) {
        blocks_[index].size = size;
        blocks_.insert(blocks_.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                       MemoryBlock::makeFree(start + size, leftover));
    }

    ++counters_.succeeded;
    return {AllocationStatus::Success, start};
}

DeallocationStatus MemoryAllocator::deallocate(ProcessId id) {
    const std::optional<Size> found = findOwner(id);
    if (!found.has_value()) {
        return DeallocationStatus::ProcessNotFound;
    }
    const Size index = *found;
    blocks_[index].state = BlockState::Free;
    blocks_[index].owner.reset();
    blocks_[index].requestedSize = 0;

    if (config_.mergeOnFree) {
        mergeAround(index);
    }
    return DeallocationStatus::Success;
}

// Two neighbours merge only if both are free and no partition boundary lies
// between them.
bool MemoryAllocator::canMerge(Size leftIndex) const {
    const MemoryBlock& left = blocks_[leftIndex];
    const MemoryBlock& right = blocks_[leftIndex + 1];
    return left.isFree() && right.isFree() && boundaries_.count(right.start) == 0;
}

void MemoryAllocator::mergeAround(Size index) {
    if (index + 1 < blocks_.size() && canMerge(index)) {
        blocks_[index].size += blocks_[index + 1].size;
        blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(index) + 1);
    }
    if (index > 0 && canMerge(index - 1)) {
        blocks_[index - 1].size += blocks_[index].size;
        blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

BlockListValidation MemoryAllocator::checkInvariants() const {
    BlockListValidation structural = validateBlockList(blocks_, totalMemory_);
    if (!structural.valid) {
        return structural;
    }

    for (const Size boundary : boundaries_) {
        const bool present = std::any_of(blocks_.begin(), blocks_.end(),
                                         [boundary](const MemoryBlock& b) { return b.start == boundary; });
        if (!present) {
            return {false, "partition boundary at " + std::to_string(boundary) + " was lost"};
        }
    }

    if (config_.mergeOnFree) {
        for (Size i = 0; i + 1 < blocks_.size(); ++i) {
            if (canMerge(i)) {
                return {false, "blocks " + std::to_string(i) + " and " + std::to_string(i + 1) +
                                   " are adjacent free blocks that should have merged"};
            }
        }
    }

    if (counters_.attempted != counters_.succeeded + counters_.failed) {
        return {false, "counters inconsistent: attempted != succeeded + failed"};
    }
    return {};
}

}  // namespace memsim
