// Tests for the comparison engine: workload replay on every placement algorithm,
// FIFO/LRU/Optimal on identical input, the algorithm factory, and frame recording.
// Expected numbers are hand-derived from the block sizes and requests in each test.

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "LRU.h"
#include "PerformanceAnalyzer.h"
#include "TestHarness.h"

using namespace memsim;

namespace {

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

const std::vector<Size> kTextbookBlocks = {100, 500, 200, 300, 600};  // total 1700

Workload textbookWorkload() {
    return {AllocationEvent::allocate(1, 212), AllocationEvent::allocate(2, 417),
            AllocationEvent::allocate(3, 112), AllocationEvent::allocate(4, 426)};
}

const std::vector<PageId> kTextbookRefs = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};

// ---------- Allocation comparison ----------

void test_compare_allocation_textbook_workload() {
    const AllocationComparison cmp = PerformanceAnalyzer::compareAllocation(kTextbookBlocks, textbookWorkload());
    CHECK_EQ(cmp.runs.size(), 3);
    CHECK_EQ(cmp.eventCount, 4);
    CHECK_EQ(cmp.blockSizes.size(), 5);

    const AllocationRunResult* first = cmp.find(PlacementAlgorithm::FirstFit);
    const AllocationRunResult* best = cmp.find(PlacementAlgorithm::BestFit);
    const AllocationRunResult* worst = cmp.find(PlacementAlgorithm::WorstFit);
    CHECK(first != nullptr && best != nullptr && worst != nullptr);
    if (first == nullptr || best == nullptr || worst == nullptr) {
        return;
    }

    // First Fit: 212, 417, 112 placed; 426 fails. Allocated 741, free 959 in holes
    // 100, 176, 200, 300, 183 (largest 300).
    CHECK_EQ(first->metrics.totalProcesses, 4);
    CHECK_EQ(first->metrics.successfulAllocations, 3);
    CHECK_EQ(first->metrics.failedAllocations, 1);
    CHECK_EQ(first->metrics.failedDespiteEnoughFree, 1);
    CHECK(near(first->metrics.successRate, 0.75));
    CHECK_EQ(first->metrics.fragmentation.allocatedMemory, 741);
    CHECK_EQ(first->metrics.fragmentation.freeMemory, 959);
    CHECK_EQ(first->metrics.fragmentation.freeBlockCount, 5);
    CHECK_EQ(first->metrics.fragmentation.largestFreeBlock, 300);
    CHECK(near(first->metrics.fragmentation.externalFragmentation, 1.0 - 300.0 / 959.0));
    CHECK_EQ(first->metrics.fragmentation.internalFragmentation, 0);
    CHECK(near(first->metrics.allocatedUtilization, 741.0 / 1700.0));
    CHECK(near(first->metrics.effectiveUtilization, 741.0 / 1700.0));
    CHECK_EQ(first->peakAllocatedMemory, 741);
    CHECK(first->failedProcessIds == std::vector<ProcessId>({4}));

    // Best Fit places all four. Allocated 1167, free 533 in holes 100, 83, 88, 88, 174.
    CHECK_EQ(best->metrics.totalProcesses, 4);
    CHECK_EQ(best->metrics.successfulAllocations, 4);
    CHECK_EQ(best->metrics.failedAllocations, 0);
    CHECK(near(best->metrics.successRate, 1.0));
    CHECK_EQ(best->metrics.fragmentation.allocatedMemory, 1167);
    CHECK_EQ(best->metrics.fragmentation.freeMemory, 533);
    CHECK_EQ(best->metrics.fragmentation.freeBlockCount, 5);
    CHECK_EQ(best->metrics.fragmentation.largestFreeBlock, 174);
    CHECK(near(best->metrics.fragmentation.externalFragmentation, 1.0 - 174.0 / 533.0));
    CHECK(near(best->metrics.allocatedUtilization, 1167.0 / 1700.0));
    CHECK_EQ(best->peakAllocatedMemory, 1167);
    CHECK(best->failedProcessIds.empty());

    // Worst Fit: same outcome counts as First Fit here, with holes 100, 83, 200, 300, 276.
    CHECK_EQ(worst->metrics.successfulAllocations, 3);
    CHECK_EQ(worst->metrics.failedAllocations, 1);
    CHECK_EQ(worst->metrics.failedDespiteEnoughFree, 1);
    CHECK_EQ(worst->metrics.fragmentation.allocatedMemory, 741);
    CHECK_EQ(worst->metrics.fragmentation.freeMemory, 959);
    CHECK_EQ(worst->metrics.fragmentation.freeBlockCount, 5);
    CHECK_EQ(worst->metrics.fragmentation.largestFreeBlock, 300);
    CHECK(worst->failedProcessIds == std::vector<ProcessId>({4}));
}

void test_compare_allocation_run_order_and_identical_input() {
    const AllocationComparison cmp = PerformanceAnalyzer::compareAllocation(kTextbookBlocks, textbookWorkload());
    CHECK(cmp.runs[0].algorithm == PlacementAlgorithm::FirstFit);
    CHECK(cmp.runs[1].algorithm == PlacementAlgorithm::BestFit);
    CHECK(cmp.runs[2].algorithm == PlacementAlgorithm::WorstFit);
    // Unique process IDs: every algorithm saw the same 4 attempts, same total memory.
    for (const auto& run : cmp.runs) {
        CHECK_EQ(run.metrics.totalProcesses, 4);
        CHECK_EQ(run.metrics.fragmentation.totalMemory, 1700);
        CHECK_EQ(run.metrics.fragmentation.allocatedMemory + run.metrics.fragmentation.freeMemory, 1700);
        CHECK_EQ(run.rejectedRequests, 0);
    }
}

void test_compare_allocation_is_repeatable() {
    const auto a = PerformanceAnalyzer::compareAllocation(kTextbookBlocks, textbookWorkload());
    const auto b = PerformanceAnalyzer::compareAllocation(kTextbookBlocks, textbookWorkload());
    for (std::size_t i = 0; i < a.runs.size(); ++i) {
        CHECK_EQ(a.runs[i].metrics.successfulAllocations, b.runs[i].metrics.successfulAllocations);
        CHECK_EQ(a.runs[i].metrics.fragmentation.freeMemory, b.runs[i].metrics.fragmentation.freeMemory);
        CHECK(a.runs[i].failedProcessIds == b.runs[i].failedProcessIds);
    }
}

void test_replay_counts_rejections_misses_and_failures() {
    // One 200-unit block.
    const Workload w = {
        AllocationEvent::allocate(1, 100),  // ok
        AllocationEvent::allocate(1, 50),   // duplicate -> rejected, not an attempt
        AllocationEvent::deallocate(1),     // ok
        AllocationEvent::deallocate(1),     // process holds nothing -> miss
        AllocationEvent::allocate(2, 0),    // invalid size -> rejected
        AllocationEvent::allocate(3, 300),  // too big -> failed attempt (free 200 < 300)
    };
    MemoryAllocator allocator({200}, PlacementAlgorithm::FirstFit);
    const AllocationRunResult run = PerformanceAnalyzer::replayWorkload(allocator, w);
    CHECK_EQ(run.metrics.totalProcesses, 2);
    CHECK_EQ(run.metrics.successfulAllocations, 1);
    CHECK_EQ(run.metrics.failedAllocations, 1);
    CHECK_EQ(run.metrics.failedDespiteEnoughFree, 0);
    CHECK(near(run.metrics.successRate, 0.5));
    CHECK_EQ(run.rejectedRequests, 2);
    CHECK_EQ(run.deallocationMisses, 1);
    CHECK_EQ(run.peakAllocatedMemory, 100);
    CHECK_EQ(run.metrics.fragmentation.allocatedMemory, 0);  // final state: everything freed
    CHECK(near(run.metrics.allocatedUtilization, 0.0));
    CHECK(run.failedProcessIds == std::vector<ProcessId>({3}));
}

void test_utilization_separates_internal_fragmentation() {
    // One 100-unit block, split threshold 30: a request of 80 leaves 20 < 30, so it stays inside.
    AllocatorConfig cfg;
    cfg.splitThreshold = 30;
    const AllocationComparison cmp = PerformanceAnalyzer::compareAllocation(
        {100}, {AllocationEvent::allocate(1, 80)}, cfg);
    for (const auto& run : cmp.runs) {
        CHECK_EQ(run.metrics.fragmentation.internalFragmentation, 20);
        CHECK(near(run.metrics.allocatedUtilization, 1.0));
        CHECK(near(run.metrics.effectiveUtilization, 0.8));
        CHECK_EQ(run.metrics.fragmentation.freeMemory, 0);
        CHECK(near(run.metrics.fragmentation.externalFragmentation, 0.0));  // no free memory: no division by zero
    }
    CHECK_EQ(cmp.config.splitThreshold, 30);
}

void test_empty_workload_gives_zero_rates() {
    const AllocationComparison cmp = PerformanceAnalyzer::compareAllocation({100, 50}, {});
    for (const auto& run : cmp.runs) {
        CHECK_EQ(run.metrics.totalProcesses, 0);
        CHECK(near(run.metrics.successRate, 0.0));
        CHECK(!std::isnan(run.metrics.successRate));
        CHECK(near(run.metrics.allocatedUtilization, 0.0));
        CHECK_EQ(run.metrics.fragmentation.freeMemory, 150);
        CHECK_EQ(run.peakAllocatedMemory, 0);
    }
}

void test_compare_allocation_rejects_invalid_memory() {
    CHECK_THROWS(PerformanceAnalyzer::compareAllocation({}, textbookWorkload()), std::invalid_argument);
    CHECK_THROWS(PerformanceAnalyzer::compareAllocation({100, 0}, textbookWorkload()), std::invalid_argument);
}

void test_analyze_allocation_on_live_allocator() {
    MemoryAllocator allocator(kTextbookBlocks, PlacementAlgorithm::BestFit);
    allocator.allocate(1, 212);
    const AllocationMetrics m = PerformanceAnalyzer::analyzeAllocation(allocator);
    CHECK_EQ(m.totalProcesses, 1);
    CHECK_EQ(m.successfulAllocations, 1);
    CHECK(near(m.successRate, 1.0));
    CHECK_EQ(m.fragmentation.allocatedMemory, 212);
    CHECK(near(m.allocatedUtilization, 212.0 / 1700.0));
}

// ---------- Algorithm factory ----------

void test_replacement_factory_and_names() {
    const auto kinds = allReplacementAlgorithms();
    CHECK_EQ(kinds.size(), 3);
    CHECK(kinds[0] == ReplacementAlgorithm::FIFO);
    CHECK(kinds[1] == ReplacementAlgorithm::LRU);
    CHECK(kinds[2] == ReplacementAlgorithm::Optimal);
    for (const ReplacementAlgorithm kind : kinds) {
        const auto algorithm = makeReplacementAlgorithm(kind);
        CHECK(algorithm != nullptr);
        if (algorithm != nullptr) {
            CHECK_EQ(algorithm->name(), toString(kind));
        }
    }
    CHECK_THROWS(makeReplacementAlgorithm(static_cast<ReplacementAlgorithm>(99)), std::invalid_argument);
}

// ---------- Page replacement comparison ----------

void test_compare_all_textbook_string() {
    const PageComparison cmp = PageReplacementSimulator::compareAll(3, kTextbookRefs);
    CHECK_EQ(cmp.frameCount, 3);
    CHECK_EQ(cmp.totalReferences, 20);
    CHECK_EQ(cmp.results.size(), 3);
    CHECK_EQ(cmp.results[0].algorithm, std::string("FIFO"));
    CHECK_EQ(cmp.results[1].algorithm, std::string("LRU"));
    CHECK_EQ(cmp.results[2].algorithm, std::string("Optimal"));
    CHECK_EQ(cmp.results[0].metrics.faults, 15);
    CHECK_EQ(cmp.results[1].metrics.faults, 12);
    CHECK_EQ(cmp.results[2].metrics.faults, 9);
    CHECK_EQ(cmp.results[0].metrics.hits, 5);
    CHECK_EQ(cmp.results[1].metrics.hits, 8);
    CHECK_EQ(cmp.results[2].metrics.hits, 11);
    CHECK_EQ(cmp.results[0].metrics.replacements, 12);
    CHECK_EQ(cmp.results[1].metrics.replacements, 9);
    CHECK_EQ(cmp.results[2].metrics.replacements, 6);
    CHECK(near(cmp.results[2].metrics.hitRatio, 0.55));
}

void test_compare_all_uses_identical_input_for_every_algorithm() {
    const PageComparison cmp = PageReplacementSimulator::compareAll(3, kTextbookRefs);
    for (const SimulationResult& result : cmp.results) {
        CHECK_EQ(result.frameCount, 3);
        CHECK_EQ(result.steps.size(), kTextbookRefs.size());
        CHECK_EQ(result.metrics.totalReferences, kTextbookRefs.size());
        for (std::size_t i = 0; i < kTextbookRefs.size(); ++i) {
            CHECK_EQ(result.steps[i].page, kTextbookRefs[i]);
        }
    }
}

void test_compare_all_matches_individual_runs() {
    const PageComparison cmp = PageReplacementSimulator::compareAll(4, kTextbookRefs);
    const ReplacementAlgorithm kinds[] = {ReplacementAlgorithm::FIFO, ReplacementAlgorithm::LRU,
                                          ReplacementAlgorithm::Optimal};
    for (std::size_t k = 0; k < 3; ++k) {
        const auto algorithm = makeReplacementAlgorithm(kinds[k]);
        const SimulationResult single = PageReplacementSimulator::run(*algorithm, 4, kTextbookRefs);
        CHECK_EQ(single.metrics.faults, cmp.results[k].metrics.faults);
        for (std::size_t i = 0; i < single.steps.size(); ++i) {
            CHECK(single.steps[i].frames == cmp.results[k].steps[i].frames);
            CHECK(single.steps[i].evicted == cmp.results[k].steps[i].evicted);
        }
    }
}

void test_compare_all_empty_references() {
    const PageComparison cmp = PageReplacementSimulator::compareAll(3, {});
    CHECK_EQ(cmp.results.size(), 3);
    for (const auto& result : cmp.results) {
        CHECK_EQ(result.metrics.totalReferences, 0);
        CHECK_EQ(result.metrics.faults, 0);
        CHECK(near(result.metrics.hitRatio, 0.0));
        CHECK(near(result.metrics.faultRatio, 0.0));
    }
}

void test_compare_all_find_by_name() {
    const PageComparison cmp = PageReplacementSimulator::compareAll(3, kTextbookRefs);
    const SimulationResult* lru = cmp.find("LRU");
    CHECK(lru != nullptr);
    if (lru != nullptr) {
        CHECK_EQ(lru->metrics.faults, 12);
    }
    CHECK(cmp.find("Clock") == nullptr);
}

void test_compare_all_invalid_input() {
    CHECK_THROWS(PageReplacementSimulator::compareAll(0, kTextbookRefs), std::invalid_argument);
    CHECK_THROWS(PageReplacementSimulator::compareAll(3, {1, -2}), std::invalid_argument);
}

void test_no_algorithm_beats_optimal_in_comparison() {
    // Workload-specific check, not a general ranking of FIFO vs LRU.
    for (Size frames = 1; frames <= 6; ++frames) {
        const PageComparison cmp = PageReplacementSimulator::compareAll(frames, kTextbookRefs);
        CHECK(cmp.results[2].metrics.faults <= cmp.results[0].metrics.faults);
        CHECK(cmp.results[2].metrics.faults <= cmp.results[1].metrics.faults);
    }
}

Size faultsOf(const PageComparison& comparison, const std::string& algorithm) {
    const SimulationResult* result = comparison.find(algorithm);
    CHECK(result != nullptr);
    return result != nullptr ? result->metrics.faults : static_cast<Size>(-1);
}

void test_beladys_anomaly_visible_in_comparison() {
    const std::vector<PageId> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    const PageComparison three = PageReplacementSimulator::compareAll(3, refs);
    const PageComparison four = PageReplacementSimulator::compareAll(4, refs);
    CHECK_EQ(faultsOf(three, "FIFO"), 9);
    CHECK_EQ(faultsOf(four, "FIFO"), 10);  // more frames, more faults
    CHECK(faultsOf(four, "LRU") <= faultsOf(three, "LRU"));
    CHECK(faultsOf(four, "Optimal") <= faultsOf(three, "Optimal"));
}

// ---------- Frame recording option ----------

void test_frames_recorded_by_default() {
    const PageComparison cmp = PageReplacementSimulator::compareAll(3, kTextbookRefs);
    for (const auto& result : cmp.results) {
        for (const StepRecord& step : result.steps) {
            CHECK_EQ(step.frames.size(), 3);
        }
    }
}

void test_disabling_frame_recording_keeps_results_identical() {
    const PageComparison with = PageReplacementSimulator::compareAll(3, kTextbookRefs);
    const PageComparison without =
        PageReplacementSimulator::compareAll(3, kTextbookRefs, SimulationOptions{false});
    for (std::size_t k = 0; k < with.results.size(); ++k) {
        CHECK_EQ(with.results[k].metrics.faults, without.results[k].metrics.faults);
        CHECK_EQ(with.results[k].metrics.hits, without.results[k].metrics.hits);
        CHECK_EQ(with.results[k].metrics.replacements, without.results[k].metrics.replacements);
        for (std::size_t i = 0; i < kTextbookRefs.size(); ++i) {
            CHECK(without.results[k].steps[i].frames.empty());
            CHECK_EQ(with.results[k].steps[i].fault, without.results[k].steps[i].fault);
            CHECK(with.results[k].steps[i].evicted == without.results[k].steps[i].evicted);
        }
    }
}

void test_performance_analyzer_page_comparison_defaults_to_metrics_only() {
    const PageComparison cmp = PerformanceAnalyzer::comparePageReplacement(3, kTextbookRefs);
    CHECK_EQ(cmp.results[1].metrics.faults, 12);
    CHECK(cmp.results[1].steps[5].frames.empty());

    const PageComparison full = PerformanceAnalyzer::comparePageReplacement(3, kTextbookRefs, SimulationOptions{true});
    CHECK_EQ(full.results[1].steps[5].frames.size(), 3);
}

void test_access_without_recording_still_updates_state() {
    // Skipping the snapshot must not skip the bookkeeping: later evictions stay correct.
    LRU lru;
    lru.reset(2, {1, 2, 1, 3});
    lru.access(1, false);
    lru.access(2, false);
    lru.access(1, false);
    const StepRecord last = lru.access(3, true);
    CHECK(last.evicted == std::optional<PageId>(2));  // 1 was refreshed, so 2 is the LRU page
    CHECK_EQ(last.frames.size(), 2);
}

}  // namespace

int main() {
    RUN_TEST(test_compare_allocation_textbook_workload);
    RUN_TEST(test_compare_allocation_run_order_and_identical_input);
    RUN_TEST(test_compare_allocation_is_repeatable);
    RUN_TEST(test_replay_counts_rejections_misses_and_failures);
    RUN_TEST(test_utilization_separates_internal_fragmentation);
    RUN_TEST(test_empty_workload_gives_zero_rates);
    RUN_TEST(test_compare_allocation_rejects_invalid_memory);
    RUN_TEST(test_analyze_allocation_on_live_allocator);

    RUN_TEST(test_replacement_factory_and_names);

    RUN_TEST(test_compare_all_textbook_string);
    RUN_TEST(test_compare_all_uses_identical_input_for_every_algorithm);
    RUN_TEST(test_compare_all_matches_individual_runs);
    RUN_TEST(test_compare_all_empty_references);
    RUN_TEST(test_compare_all_find_by_name);
    RUN_TEST(test_compare_all_invalid_input);
    RUN_TEST(test_no_algorithm_beats_optimal_in_comparison);
    RUN_TEST(test_beladys_anomaly_visible_in_comparison);

    RUN_TEST(test_frames_recorded_by_default);
    RUN_TEST(test_disabling_frame_recording_keeps_results_identical);
    RUN_TEST(test_performance_analyzer_page_comparison_defaults_to_metrics_only);
    RUN_TEST(test_access_without_recording_still_updates_state);

    return TEST_SUMMARY();
}
