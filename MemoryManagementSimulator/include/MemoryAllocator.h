#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "MemoryBlock.h"
#include "PlacementStrategy.h"
#include "Types.h"

namespace memsim {

struct AllocatorConfig {
    // A leftover smaller than this is NOT split off; it stays inside the
    // allocated block as internal fragmentation. 1 = pure dynamic partitioning.
    // Must be >= 1.
    Size splitThreshold{1};
    // Coalesce adjacent free blocks when a process is deallocated.
    bool mergeOnFree{true};

    // Fixed partitions: never split, never merge.
    static AllocatorConfig fixedPartitions() {
        return AllocatorConfig{std::numeric_limits<Size>::max(), false};
    }
};

enum class AllocationStatus { Success, NoSuitableBlock, DuplicateProcess, InvalidSize, InvalidProcessId };
enum class DeallocationStatus { Success, ProcessNotFound };

std::string toString(AllocationStatus status);
std::string toString(DeallocationStatus status);

struct AllocationResult {
    AllocationStatus status{AllocationStatus::Success};
    std::optional<Size> blockStart{};  // address of the allocated block on success
    bool succeeded() const noexcept { return status == AllocationStatus::Success; }
};

// Only requests that reach block selection count as attempts. Requests rejected
// by validation (bad size/ID, duplicate process) are not attempts.
struct AllocationCounters {
    Size attempted{0};
    Size succeeded{0};
    Size failed{0};                         // attempts with no suitable block
    Size failedDespiteEnoughTotalFree{0};   // failed although total free >= request (external fragmentation)
};

// Owns the address-ordered block list and keeps it consistent.
//
// Initial block sizes define partitions. Allocation may split a block inside its
// partition; deallocation merges adjacent free blocks only within the same
// partition, so the configured partition boundaries are never destroyed.
class MemoryAllocator {
public:
    // Throws std::invalid_argument for invalid block sizes (see makeInitialBlocks)
    // or splitThreshold == 0.
    MemoryAllocator(const std::vector<Size>& blockSizes, PlacementAlgorithm algorithm,
                    AllocatorConfig config = AllocatorConfig{});

    AllocationResult allocate(ProcessId id, Size size);
    DeallocationStatus deallocate(ProcessId id);

    void setAlgorithm(PlacementAlgorithm algorithm);
    PlacementAlgorithm algorithm() const noexcept { return algorithm_; }

    const std::vector<MemoryBlock>& blocks() const noexcept { return blocks_; }
    Size totalMemory() const noexcept { return totalMemory_; }
    const AllocationCounters& counters() const noexcept { return counters_; }
    const AllocatorConfig& config() const noexcept { return config_; }

    // Structural validity, partition boundaries preserved, merge rule honored,
    // counters consistent. Used by tests after every operation.
    BlockListValidation checkInvariants() const;

private:
    std::optional<Size> findOwner(ProcessId id) const;
    Size freeMemory() const;
    bool canMerge(Size leftIndex) const;
    void mergeAround(Size index);

    std::vector<MemoryBlock> blocks_;
    std::set<Size> boundaries_;  // start addresses of the initial partitions
    Size totalMemory_;
    PlacementAlgorithm algorithm_;
    std::unique_ptr<PlacementStrategy> strategy_;
    AllocatorConfig config_;
    AllocationCounters counters_{};
};

}  // namespace memsim
