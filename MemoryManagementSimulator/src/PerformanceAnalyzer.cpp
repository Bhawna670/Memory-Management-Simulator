#include "PerformanceAnalyzer.h"

#include <algorithm>

namespace memsim {

namespace {

double ratio(Size numerator, Size denominator) {
    if (denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

}  // namespace

const AllocationRunResult* AllocationComparison::find(PlacementAlgorithm algorithm) const {
    for (const AllocationRunResult& run : runs) {
        if (run.algorithm == algorithm) {
            return &run;
        }
    }
    return nullptr;
}

AllocationMetrics PerformanceAnalyzer::analyzeAllocation(const MemoryAllocator& allocator) {
    const AllocationCounters& counters = allocator.counters();

    AllocationMetrics metrics;
    metrics.totalProcesses = counters.attempted;
    metrics.successfulAllocations = counters.succeeded;
    metrics.failedAllocations = counters.failed;
    metrics.failedDespiteEnoughFree = counters.failedDespiteEnoughTotalFree;
    metrics.successRate = ratio(counters.succeeded, counters.attempted);
    metrics.fragmentation = FragmentationAnalyzer::analyze(allocator.blocks());
    metrics.allocatedUtilization =
        ratio(metrics.fragmentation.allocatedMemory, metrics.fragmentation.totalMemory);
    metrics.effectiveUtilization =
        ratio(metrics.fragmentation.requestedMemory, metrics.fragmentation.totalMemory);
    return metrics;
}

AllocationRunResult PerformanceAnalyzer::replayWorkload(MemoryAllocator& allocator,
                                                        const Workload& workload) {
    AllocationRunResult run;
    run.algorithm = allocator.algorithm();

    for (const AllocationEvent& event : workload) {
        if (event.kind == AllocationEvent::Kind::Allocate) {
            const AllocationResult result = allocator.allocate(event.id, event.size);
            switch (result.status) {
                case AllocationStatus::Success:
                    break;
                case AllocationStatus::NoSuitableBlock:
                    run.failedProcessIds.push_back(event.id);
                    break;
                case AllocationStatus::DuplicateProcess:
                case AllocationStatus::InvalidSize:
                case AllocationStatus::InvalidProcessId:
                    ++run.rejectedRequests;
                    break;
            }
        } else if (allocator.deallocate(event.id) == DeallocationStatus::ProcessNotFound) {
            ++run.deallocationMisses;
        }
        run.peakAllocatedMemory = std::max(
            run.peakAllocatedMemory, FragmentationAnalyzer::analyze(allocator.blocks()).allocatedMemory);
    }

    run.metrics = analyzeAllocation(allocator);
    return run;
}

AllocationComparison PerformanceAnalyzer::compareAllocation(const std::vector<Size>& blockSizes,
                                                            const Workload& workload,
                                                            AllocatorConfig config) {
    AllocationComparison comparison;
    comparison.blockSizes = blockSizes;
    comparison.config = config;
    comparison.eventCount = workload.size();

    for (const PlacementAlgorithm algorithm :
         {PlacementAlgorithm::FirstFit, PlacementAlgorithm::BestFit, PlacementAlgorithm::WorstFit}) {
        MemoryAllocator allocator(blockSizes, algorithm, config);
        comparison.runs.push_back(replayWorkload(allocator, workload));
    }
    return comparison;
}

PageComparison PerformanceAnalyzer::comparePageReplacement(Size frameCount,
                                                           const std::vector<PageId>& references,
                                                           SimulationOptions options) {
    return PageReplacementSimulator::compareAll(frameCount, references, options);
}

}  // namespace memsim
