// MemoryAllocator and FragmentationAnalyzer tests.
// Expected values are hand-derived from the block sizes and requests stated in
// each test (classic textbook set {100, 500, 200, 300, 600} with requests
// 212, 417, 112, 426).

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "FragmentationAnalyzer.h"
#include "MemoryAllocator.h"
#include "TestHarness.h"

using namespace memsim;

namespace {

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

std::optional<Size> at(Size address) { return address; }

// Checks every allocator invariant; message names the failing rule.
void expectValid(const MemoryAllocator& allocator, int line) {
    const BlockListValidation v = allocator.checkInvariants();
    const std::string text = "allocator invariants (line " + std::to_string(line) + "): " + v.message;
    ::testing::check(v.valid, text.c_str(), __FILE__, line);
}
#define EXPECT_VALID(allocator) expectValid((allocator), __LINE__)

FragmentationReport report(const MemoryAllocator& allocator) {
    return FragmentationAnalyzer::analyze(allocator.blocks());
}

const std::vector<Size> kTextbook = {100, 500, 200, 300, 600};  // total 1700

// ---------- Allocation basics ----------

void test_exact_size_allocation() {
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit);
    const AllocationResult r = a.allocate(1, 100);
    CHECK(r.succeeded());
    CHECK(r.blockStart == at(0));
    CHECK_EQ(a.blocks().size(), 1);  // nothing left to split off
    CHECK(!a.blocks()[0].isFree());
    CHECK_EQ(a.blocks()[0].requestedSize, 100);
    const auto rep = report(a);
    CHECK_EQ(rep.internalFragmentation, 0);
    CHECK_EQ(rep.freeMemory, 0);
    CHECK_EQ(rep.allocatedMemory, 100);
    EXPECT_VALID(a);
}

void test_allocation_with_remaining_space_splits_block() {
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 60).succeeded());
    CHECK_EQ(a.blocks().size(), 2);
    CHECK_EQ(a.blocks()[0].start, 0);
    CHECK_EQ(a.blocks()[0].size, 60);
    CHECK(!a.blocks()[0].isFree());
    CHECK_EQ(a.blocks()[1].start, 60);
    CHECK_EQ(a.blocks()[1].size, 40);
    CHECK(a.blocks()[1].isFree());
    const auto rep = report(a);
    CHECK_EQ(rep.allocatedMemory, 60);
    CHECK_EQ(rep.freeMemory, 40);
    CHECK_EQ(rep.internalFragmentation, 0);  // the remainder is a free block, not internal waste
    EXPECT_VALID(a);
}

void test_split_threshold_keeps_small_remainder_as_internal_fragmentation() {
    AllocatorConfig cfg;
    cfg.splitThreshold = 50;
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit, cfg);
    CHECK(a.allocate(1, 60).succeeded());  // leftover 40 < 50: not split
    CHECK_EQ(a.blocks().size(), 1);
    CHECK_EQ(a.blocks()[0].size, 100);
    CHECK_EQ(a.blocks()[0].requestedSize, 60);
    const auto rep = report(a);
    CHECK_EQ(rep.internalFragmentation, 40);
    CHECK_EQ(rep.freeMemory, 0);
    CHECK_EQ(rep.allocatedMemory, 100);
    CHECK_EQ(rep.requestedMemory, 60);
    EXPECT_VALID(a);
}

void test_remainder_equal_to_threshold_is_split() {
    AllocatorConfig cfg;
    cfg.splitThreshold = 50;
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit, cfg);
    CHECK(a.allocate(1, 50).succeeded());  // leftover 50 >= 50: split
    CHECK_EQ(a.blocks().size(), 2);
    CHECK_EQ(report(a).internalFragmentation, 0);
    EXPECT_VALID(a);
}

void test_fixed_partitions_never_split_or_merge() {
    MemoryAllocator a({100, 200}, PlacementAlgorithm::FirstFit, AllocatorConfig::fixedPartitions());
    CHECK(a.allocate(1, 30).succeeded());
    CHECK_EQ(a.blocks().size(), 2);
    CHECK_EQ(a.blocks()[0].size, 100);
    CHECK_EQ(report(a).internalFragmentation, 70);
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK_EQ(a.blocks().size(), 2);
    CHECK(a.blocks()[0].isFree());
    EXPECT_VALID(a);
}

// ---------- Failures and validation ----------

void test_no_suitable_block() {
    MemoryAllocator a({100, 50}, PlacementAlgorithm::FirstFit);
    const AllocationResult r = a.allocate(1, 200);
    CHECK(r.status == AllocationStatus::NoSuitableBlock);
    CHECK(!r.blockStart.has_value());
    CHECK_EQ(a.counters().attempted, 1);
    CHECK_EQ(a.counters().failed, 1);
    CHECK_EQ(a.counters().succeeded, 0);
    CHECK_EQ(a.counters().failedDespiteEnoughTotalFree, 0);  // only 150 free in total
    CHECK_EQ(a.blocks().size(), 2);                          // state untouched
    CHECK_EQ(report(a).freeMemory, 150);
    EXPECT_VALID(a);
}

void test_failure_caused_by_external_fragmentation() {
    MemoryAllocator a({300}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 100).succeeded());
    CHECK(a.allocate(2, 100).succeeded());
    CHECK(a.allocate(3, 100).succeeded());
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK(a.deallocate(3) == DeallocationStatus::Success);
    // free blocks {0..100} and {200..300}, separated by process 2
    const auto rep = report(a);
    CHECK_EQ(rep.freeMemory, 200);
    CHECK_EQ(rep.freeBlockCount, 2);
    CHECK_EQ(rep.largestFreeBlock, 100);
    CHECK(near(rep.externalFragmentation, 0.5));

    const AllocationResult r = a.allocate(4, 150);  // 200 free in total, but no single 150 block
    CHECK(r.status == AllocationStatus::NoSuitableBlock);
    CHECK_EQ(a.counters().failed, 1);
    CHECK_EQ(a.counters().failedDespiteEnoughTotalFree, 1);
    EXPECT_VALID(a);
}

void test_failed_allocation_can_succeed_after_deallocation() {
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 100).succeeded());
    CHECK(a.allocate(2, 40).status == AllocationStatus::NoSuitableBlock);
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK(a.allocate(2, 40).succeeded());
    CHECK_EQ(a.counters().attempted, 3);
    CHECK_EQ(a.counters().succeeded, 2);
    CHECK_EQ(a.counters().failed, 1);
    EXPECT_VALID(a);
}

void test_invalid_requests_are_not_attempts() {
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 0).status == AllocationStatus::InvalidSize);
    CHECK(a.allocate(0, 10).status == AllocationStatus::InvalidProcessId);
    CHECK(a.allocate(-3, 10).status == AllocationStatus::InvalidProcessId);
    CHECK(a.allocate(1, 10).succeeded());
    CHECK(a.allocate(1, 10).status == AllocationStatus::DuplicateProcess);
    CHECK_EQ(a.counters().attempted, 1);  // only the one valid request
    CHECK_EQ(a.blocks().size(), 2);
    EXPECT_VALID(a);
}

void test_deallocate_unknown_process() {
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit);
    CHECK(a.deallocate(1) == DeallocationStatus::ProcessNotFound);
    CHECK(a.allocate(1, 10).succeeded());
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK(a.deallocate(1) == DeallocationStatus::ProcessNotFound);  // already freed
}

void test_process_id_reusable_after_deallocation() {
    MemoryAllocator a({100}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 10).succeeded());
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK(a.allocate(1, 20).succeeded());
    EXPECT_VALID(a);
}

void test_constructor_validation() {
    CHECK_THROWS(MemoryAllocator({}, PlacementAlgorithm::FirstFit), std::invalid_argument);
    CHECK_THROWS(MemoryAllocator({100, 0}, PlacementAlgorithm::FirstFit), std::invalid_argument);
    AllocatorConfig bad;
    bad.splitThreshold = 0;
    CHECK_THROWS(MemoryAllocator({100}, PlacementAlgorithm::FirstFit, bad), std::invalid_argument);
}

// ---------- Textbook workload: 212, 417, 112, 426 on {100, 500, 200, 300, 600} ----------
// Partition starts: 0, 100, 600, 800, 1100.

void test_first_fit_textbook_workload() {
    MemoryAllocator a(kTextbook, PlacementAlgorithm::FirstFit);
    const auto p1 = a.allocate(1, 212);  // first block >= 212 is the 500 block at 100
    const auto p2 = a.allocate(2, 417);  // 500 block now holds 212 + 288 free; first >= 417 is 600 at 1100
    const auto p3 = a.allocate(3, 112);  // 288 remainder at 312
    const auto p4 = a.allocate(4, 426);  // nothing large enough remains
    CHECK(p1.blockStart == at(100));
    CHECK(p2.blockStart == at(1100));
    CHECK(p3.blockStart == at(312));
    CHECK(p4.status == AllocationStatus::NoSuitableBlock);
    CHECK_EQ(a.counters().attempted, 4);
    CHECK_EQ(a.counters().succeeded, 3);
    CHECK_EQ(a.counters().failed, 1);
    CHECK_EQ(a.counters().failedDespiteEnoughTotalFree, 1);  // 959 free, but largest hole is 300
    const auto rep = report(a);
    CHECK_EQ(rep.totalMemory, 1700);
    CHECK_EQ(rep.allocatedMemory, 741);  // 212 + 417 + 112
    CHECK_EQ(rep.freeMemory, 959);
    CHECK_EQ(rep.largestFreeBlock, 300);
    EXPECT_VALID(a);
}

void test_best_fit_textbook_workload() {
    MemoryAllocator a(kTextbook, PlacementAlgorithm::BestFit);
    const auto p1 = a.allocate(1, 212);  // smallest block >= 212 is 300 at 800
    const auto p2 = a.allocate(2, 417);  // 500 at 100
    const auto p3 = a.allocate(3, 112);  // 200 at 600
    const auto p4 = a.allocate(4, 426);  // 600 at 1100
    CHECK(p1.blockStart == at(800));
    CHECK(p2.blockStart == at(100));
    CHECK(p3.blockStart == at(600));
    CHECK(p4.blockStart == at(1100));
    CHECK_EQ(a.counters().succeeded, 4);
    CHECK_EQ(a.counters().failed, 0);
    const auto rep = report(a);
    CHECK_EQ(rep.allocatedMemory, 1167);  // 212 + 417 + 112 + 426
    CHECK_EQ(rep.freeMemory, 533);
    EXPECT_VALID(a);
}

void test_worst_fit_textbook_workload() {
    MemoryAllocator a(kTextbook, PlacementAlgorithm::WorstFit);
    const auto p1 = a.allocate(1, 212);  // largest block is 600 at 1100 (388 left at 1312)
    const auto p2 = a.allocate(2, 417);  // largest >= 417 is 500 at 100
    const auto p3 = a.allocate(3, 112);  // largest is the 388 remainder at 1312
    const auto p4 = a.allocate(4, 426);  // nothing large enough remains
    CHECK(p1.blockStart == at(1100));
    CHECK(p2.blockStart == at(100));
    CHECK(p3.blockStart == at(1312));
    CHECK(p4.status == AllocationStatus::NoSuitableBlock);
    const auto rep = report(a);
    CHECK_EQ(rep.allocatedMemory, 741);
    CHECK_EQ(rep.freeMemory, 959);
    EXPECT_VALID(a);
}

void test_set_algorithm_changes_selection() {
    MemoryAllocator a({300, 120, 500}, PlacementAlgorithm::FirstFit);
    CHECK(a.algorithm() == PlacementAlgorithm::FirstFit);
    a.setAlgorithm(PlacementAlgorithm::BestFit);
    CHECK(a.algorithm() == PlacementAlgorithm::BestFit);
    const auto r = a.allocate(1, 100);
    CHECK(r.blockStart == at(300));  // the 120 block; First Fit would have chosen address 0
    EXPECT_VALID(a);
}

// ---------- Deallocation and merging ----------

void test_merge_with_next_and_previous_neighbours() {
    MemoryAllocator a({300}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 100).succeeded());
    CHECK(a.allocate(2, 100).succeeded());
    CHECK(a.allocate(3, 100).succeeded());
    CHECK_EQ(a.blocks().size(), 3);

    CHECK(a.deallocate(2) == DeallocationStatus::Success);  // neighbours are allocated: no merge
    CHECK_EQ(a.blocks().size(), 3);
    EXPECT_VALID(a);

    CHECK(a.deallocate(1) == DeallocationStatus::Success);  // merges with the free NEXT block
    CHECK_EQ(a.blocks().size(), 2);
    CHECK_EQ(a.blocks()[0].size, 200);
    EXPECT_VALID(a);

    CHECK(a.deallocate(3) == DeallocationStatus::Success);  // merges with the free PREVIOUS block
    CHECK_EQ(a.blocks().size(), 1);
    CHECK_EQ(a.blocks()[0].size, 300);
    CHECK(a.blocks()[0].isFree());
    EXPECT_VALID(a);
}

void test_merge_both_sides_at_once() {
    MemoryAllocator a({300}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 100).succeeded());
    CHECK(a.allocate(2, 100).succeeded());
    CHECK(a.allocate(3, 100).succeeded());
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK(a.deallocate(3) == DeallocationStatus::Success);
    CHECK_EQ(a.blocks().size(), 3);
    CHECK(a.deallocate(2) == DeallocationStatus::Success);  // free on both sides: collapses to one block
    CHECK_EQ(a.blocks().size(), 1);
    CHECK_EQ(a.blocks()[0].size, 300);
    EXPECT_VALID(a);
}

void test_merging_can_be_disabled() {
    AllocatorConfig cfg;
    cfg.mergeOnFree = false;
    MemoryAllocator a({300}, PlacementAlgorithm::FirstFit, cfg);
    CHECK(a.allocate(1, 100).succeeded());
    CHECK(a.allocate(2, 100).succeeded());
    CHECK(a.allocate(3, 100).succeeded());
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK(a.deallocate(2) == DeallocationStatus::Success);
    CHECK(a.deallocate(3) == DeallocationStatus::Success);
    CHECK_EQ(a.blocks().size(), 3);  // stays fragmented
    CHECK_EQ(report(a).freeBlockCount, 3);
    EXPECT_VALID(a);
}

void test_merge_never_crosses_partition_boundary() {
    MemoryAllocator a({100, 100}, PlacementAlgorithm::FirstFit);
    CHECK(a.allocate(1, 60).succeeded());  // splits partition 0 into 60 + 40
    CHECK_EQ(a.blocks().size(), 3);
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    // 60 + 40 merge back into partition 0; partition 1 (also free) is NOT absorbed
    CHECK_EQ(a.blocks().size(), 2);
    CHECK_EQ(a.blocks()[0].size, 100);
    CHECK_EQ(a.blocks()[1].start, 100);
    CHECK_EQ(a.blocks()[1].size, 100);
    EXPECT_VALID(a);
}

void test_deallocation_restores_accounting() {
    MemoryAllocator a(kTextbook, PlacementAlgorithm::BestFit);
    CHECK(a.allocate(1, 212).succeeded());
    CHECK(a.allocate(2, 417).succeeded());
    CHECK(a.deallocate(1) == DeallocationStatus::Success);
    CHECK(a.deallocate(2) == DeallocationStatus::Success);
    const auto rep = report(a);
    CHECK_EQ(rep.allocatedMemory, 0);
    CHECK_EQ(rep.freeMemory, 1700);
    CHECK_EQ(rep.requestedMemory, 0);
    CHECK_EQ(a.blocks().size(), 5);  // original partitions fully restored
    for (Size i = 0; i < a.blocks().size(); ++i) {
        CHECK_EQ(a.blocks()[i].size, kTextbook[i]);
    }
    EXPECT_VALID(a);
}

// ---------- Accounting consistency under a long deterministic workload ----------

void runStress(PlacementAlgorithm algorithm, Size splitThreshold) {
    AllocatorConfig cfg;
    cfg.splitThreshold = splitThreshold;
    MemoryAllocator a({64, 128, 256, 512}, algorithm, cfg);

    std::uint32_t state = 12345u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state >> 16;
    };

    std::vector<ProcessId> live;
    ProcessId nextId = 1;
    for (int step = 0; step < 400; ++step) {
        if (live.empty() || next() % 3 != 0) {
            const Size size = 1 + next() % 150;
            if (a.allocate(nextId, size).succeeded()) {
                live.push_back(nextId);
            }
            ++nextId;
        } else {
            const Size pick = next() % live.size();
            CHECK(a.deallocate(live[pick]) == DeallocationStatus::Success);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(pick));
        }

        const auto rep = report(a);
        CHECK_EQ(rep.allocatedMemory + rep.freeMemory, 960);
        CHECK_EQ(rep.totalMemory, 960);
        CHECK(rep.requestedMemory <= rep.allocatedMemory);
        CHECK_EQ(rep.internalFragmentation, rep.allocatedMemory - rep.requestedMemory);
        const BlockListValidation v = a.checkInvariants();
        if (!v.valid) {
            CHECK(v.valid);  // records the failure once, then stops this run
            std::cerr << "  step " << step << ": " << v.message << "\n";
            return;
        }
    }
    CHECK_EQ(a.counters().attempted, a.counters().succeeded + a.counters().failed);
}

void test_accounting_consistent_under_stress_first_fit() { runStress(PlacementAlgorithm::FirstFit, 1); }
void test_accounting_consistent_under_stress_best_fit() { runStress(PlacementAlgorithm::BestFit, 1); }
void test_accounting_consistent_under_stress_worst_fit() { runStress(PlacementAlgorithm::WorstFit, 1); }
void test_accounting_consistent_with_internal_fragmentation() { runStress(PlacementAlgorithm::BestFit, 16); }

// ---------- Extreme values ----------

void test_extreme_sizes_do_not_overflow() {
    const Size max = std::numeric_limits<Size>::max();
    const Size big = max - 10;
    MemoryAllocator a({big, 10}, PlacementAlgorithm::FirstFit);  // total is exactly the largest representable size
    CHECK(a.totalMemory() == max);

    CHECK(a.allocate(1, max).status == AllocationStatus::NoSuitableBlock);  // larger than any single block
    CHECK(a.allocate(2, big).succeeded());                                  // exact fit of the huge block
    CHECK_EQ(a.blocks().size(), 2);
    const auto rep = report(a);
    CHECK(rep.allocatedMemory == big);
    CHECK_EQ(rep.freeMemory, 10);
    CHECK(near(rep.externalFragmentation, 0.0));
    CHECK(a.allocate(3, 11).status == AllocationStatus::NoSuitableBlock);
    CHECK(a.allocate(3, 10).succeeded());
    CHECK(a.deallocate(2) == DeallocationStatus::Success);
    CHECK(a.deallocate(3) == DeallocationStatus::Success);
    CHECK_EQ(a.blocks().size(), 2);  // partition boundary preserved
    EXPECT_VALID(a);
}

// ---------- FragmentationAnalyzer ----------

void test_analyzer_hand_computed_layout() {
    const std::vector<MemoryBlock> blocks = {
        MemoryBlock::makeAllocated(0, 100, 1, 60),     // internal 40
        MemoryBlock::makeFree(100, 50),
        MemoryBlock::makeAllocated(150, 100, 2, 100),  // internal 0
        MemoryBlock::makeFree(250, 150),
        MemoryBlock::makeAllocated(400, 50, 3, 45),    // internal 5
    };
    const auto r = FragmentationAnalyzer::analyze(blocks);
    CHECK_EQ(r.totalMemory, 450);
    CHECK_EQ(r.allocatedMemory, 250);
    CHECK_EQ(r.requestedMemory, 205);
    CHECK_EQ(r.internalFragmentation, 45);
    CHECK_EQ(r.freeMemory, 200);
    CHECK_EQ(r.freeBlockCount, 2);
    CHECK_EQ(r.largestFreeBlock, 150);
    CHECK(near(r.externalFragmentation, 0.25));  // 1 - 150/200
}

void test_free_block_slack_is_not_internal_fragmentation() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeAllocated(0, 10, 1, 10),
                                             MemoryBlock::makeFree(10, 990)};
    const auto r = FragmentationAnalyzer::analyze(blocks);
    CHECK_EQ(r.internalFragmentation, 0);
    CHECK_EQ(r.freeMemory, 990);
}

void test_single_free_block_has_zero_external_fragmentation() {
    const auto r = FragmentationAnalyzer::analyze({MemoryBlock::makeFree(0, 500)});
    CHECK_EQ(r.freeBlockCount, 1);
    CHECK(near(r.externalFragmentation, 0.0));
}

void test_no_free_memory_does_not_divide_by_zero() {
    const auto r = FragmentationAnalyzer::analyze({MemoryBlock::makeAllocated(0, 100, 1, 80)});
    CHECK_EQ(r.freeMemory, 0);
    CHECK_EQ(r.freeBlockCount, 0);
    CHECK_EQ(r.largestFreeBlock, 0);
    CHECK(near(r.externalFragmentation, 0.0));
    CHECK(!std::isnan(r.externalFragmentation));
}

void test_empty_block_list_gives_zero_report() {
    const auto r = FragmentationAnalyzer::analyze({});
    CHECK_EQ(r.totalMemory, 0);
    CHECK_EQ(r.freeBlockCount, 0);
    CHECK(near(r.externalFragmentation, 0.0));
}

void test_external_indicator_formula() {
    CHECK(near(FragmentationAnalyzer::externalFragmentationIndicator(100, 200), 0.5));
    CHECK(near(FragmentationAnalyzer::externalFragmentationIndicator(300, 1000), 0.7));
    CHECK(near(FragmentationAnalyzer::externalFragmentationIndicator(500, 500), 0.0));
    CHECK(near(FragmentationAnalyzer::externalFragmentationIndicator(0, 0), 0.0));
}

void test_internal_and_external_are_reported_separately() {
    // Two free holes (external) and one oversized allocation (internal) at the same time.
    AllocatorConfig cfg;
    cfg.splitThreshold = 20;
    MemoryAllocator a({100, 100, 100}, PlacementAlgorithm::FirstFit, cfg);
    CHECK(a.allocate(1, 90).succeeded());  // leftover 10 < 20: internal fragmentation 10
    const auto r = report(a);
    CHECK_EQ(r.internalFragmentation, 10);
    CHECK_EQ(r.freeBlockCount, 2);
    CHECK_EQ(r.largestFreeBlock, 100);
    CHECK(near(r.externalFragmentation, 0.5));  // 1 - 100/200
    EXPECT_VALID(a);
}

}  // namespace

int main() {
    RUN_TEST(test_exact_size_allocation);
    RUN_TEST(test_allocation_with_remaining_space_splits_block);
    RUN_TEST(test_split_threshold_keeps_small_remainder_as_internal_fragmentation);
    RUN_TEST(test_remainder_equal_to_threshold_is_split);
    RUN_TEST(test_fixed_partitions_never_split_or_merge);

    RUN_TEST(test_no_suitable_block);
    RUN_TEST(test_failure_caused_by_external_fragmentation);
    RUN_TEST(test_failed_allocation_can_succeed_after_deallocation);
    RUN_TEST(test_invalid_requests_are_not_attempts);
    RUN_TEST(test_deallocate_unknown_process);
    RUN_TEST(test_process_id_reusable_after_deallocation);
    RUN_TEST(test_constructor_validation);

    RUN_TEST(test_first_fit_textbook_workload);
    RUN_TEST(test_best_fit_textbook_workload);
    RUN_TEST(test_worst_fit_textbook_workload);
    RUN_TEST(test_set_algorithm_changes_selection);

    RUN_TEST(test_merge_with_next_and_previous_neighbours);
    RUN_TEST(test_merge_both_sides_at_once);
    RUN_TEST(test_merging_can_be_disabled);
    RUN_TEST(test_merge_never_crosses_partition_boundary);
    RUN_TEST(test_deallocation_restores_accounting);
    RUN_TEST(test_extreme_sizes_do_not_overflow);

    RUN_TEST(test_accounting_consistent_under_stress_first_fit);
    RUN_TEST(test_accounting_consistent_under_stress_best_fit);
    RUN_TEST(test_accounting_consistent_under_stress_worst_fit);
    RUN_TEST(test_accounting_consistent_with_internal_fragmentation);

    RUN_TEST(test_analyzer_hand_computed_layout);
    RUN_TEST(test_free_block_slack_is_not_internal_fragmentation);
    RUN_TEST(test_single_free_block_has_zero_external_fragmentation);
    RUN_TEST(test_no_free_memory_does_not_divide_by_zero);
    RUN_TEST(test_empty_block_list_gives_zero_report);
    RUN_TEST(test_external_indicator_formula);
    RUN_TEST(test_internal_and_external_are_reported_separately);

    return TEST_SUMMARY();
}
