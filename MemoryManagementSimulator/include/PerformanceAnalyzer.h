#pragma once

#include <vector>

#include "FragmentationAnalyzer.h"
#include "MemoryAllocator.h"
#include "PageReplacementSimulator.h"
#include "PlacementStrategy.h"
#include "Types.h"
#include "Workload.h"

namespace memsim {

// Allocation metrics for the allocator's current state and history.
//   totalProcesses counts allocation attempts that reached block selection
//   (requests rejected by validation are not attempts).
//   successRate = successful / totalProcesses, 0.0 when there were no attempts.
//   allocatedUtilization = allocated block bytes / total memory
//   effectiveUtilization = bytes actually requested / total memory
//     (the gap between the two is internal fragmentation)
struct AllocationMetrics {
    Size totalProcesses{0};
    Size successfulAllocations{0};
    Size failedAllocations{0};
    Size failedDespiteEnoughFree{0};  // total free >= request, but no single block fit
    double successRate{0.0};
    double allocatedUtilization{0.0};
    double effectiveUtilization{0.0};
    FragmentationReport fragmentation{};  // internal fragmentation, external indicator, free blocks...
};

struct AllocationRunResult {
    PlacementAlgorithm algorithm{PlacementAlgorithm::FirstFit};
    AllocationMetrics metrics{};            // state after the last event
    Size rejectedRequests{0};               // invalid size/ID or duplicate process (not attempts)
    Size deallocationMisses{0};             // deallocate of a process holding nothing
    Size peakAllocatedMemory{0};            // highest allocated bytes after any event
    std::vector<ProcessId> failedProcessIds{};  // attempts that found no suitable block, in order
};

struct AllocationComparison {
    std::vector<Size> blockSizes{};
    AllocatorConfig config{};
    Size eventCount{0};
    std::vector<AllocationRunResult> runs{};  // First Fit, Best Fit, Worst Fit

    const AllocationRunResult* find(PlacementAlgorithm algorithm) const;
};

// Computes and compares metrics. Returns data only; formatting belongs to TablePrinter.
class PerformanceAnalyzer {
public:
    static AllocationMetrics analyzeAllocation(const MemoryAllocator& allocator);

    // Applies every event to `allocator` (which should be fresh for a fair measurement;
    // counters accumulate across calls).
    static AllocationRunResult replayWorkload(MemoryAllocator& allocator, const Workload& workload);

    // Replays one workload on a fresh allocator per placement algorithm, all built from
    // the same block sizes and config. Throws std::invalid_argument for invalid sizes/config.
    // Note: an algorithm's own outcomes decide which later requests count as duplicates,
    // so attempt counts can differ between algorithms if process IDs are reused.
    static AllocationComparison compareAllocation(const std::vector<Size>& blockSizes,
                                                  const Workload& workload,
                                                  AllocatorConfig config = AllocatorConfig{});

    // FIFO, LRU and Optimal on identical input. Frames are not recorded by default
    // (metrics only); pass SimulationOptions{true} to keep per-step frame tables.
    static PageComparison comparePageReplacement(Size frameCount,
                                                 const std::vector<PageId>& references,
                                                 SimulationOptions options = SimulationOptions{false});
};

}  // namespace memsim
