// Edge cases and defensive paths found by coverage analysis: user-facing status texts,
// invalid enum values, direct API misuse, and arithmetic limits.

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "FIFO.h"
#include "MemoryAllocator.h"
#include "MemoryBlock.h"
#include "PageReplacementSimulator.h"
#include "PerformanceAnalyzer.h"
#include "PlacementStrategy.h"
#include "ProcessRegistry.h"
#include "TestHarness.h"

using namespace memsim;

namespace {

// ---------- user-facing texts ----------

void test_process_and_block_state_texts() {
    CHECK_EQ(toString(ProcessState::Pending), std::string("Pending"));
    CHECK_EQ(toString(ProcessState::Allocated), std::string("Allocated"));
    CHECK_EQ(toString(ProcessState::Failed), std::string("Failed"));
    CHECK_EQ(toString(static_cast<ProcessState>(99)), std::string("Unknown"));
    CHECK_EQ(toString(BlockState::Free), std::string("Free"));
    CHECK_EQ(toString(BlockState::Allocated), std::string("Allocated"));
}

void test_registry_status_texts() {
    CHECK_EQ(toString(RegistryStatus::Success), std::string("Success"));
    CHECK_EQ(toString(RegistryStatus::InvalidId), std::string("Process ID must be a positive integer"));
    CHECK_EQ(toString(RegistryStatus::InvalidSize), std::string("Process size must be greater than 0"));
    CHECK_EQ(toString(RegistryStatus::DuplicateId), std::string("A process with this ID already exists"));
    CHECK_EQ(toString(RegistryStatus::NotFound), std::string("No process with this ID exists"));
    CHECK_EQ(toString(static_cast<RegistryStatus>(99)), std::string("Unknown"));
}

void test_allocation_status_texts() {
    CHECK_EQ(toString(AllocationStatus::Success), std::string("Success"));
    CHECK_EQ(toString(AllocationStatus::NoSuitableBlock), std::string("No suitable free block"));
    CHECK_EQ(toString(AllocationStatus::DuplicateProcess), std::string("Process already holds an allocation"));
    CHECK_EQ(toString(AllocationStatus::InvalidSize), std::string("Process size must be greater than 0"));
    CHECK_EQ(toString(AllocationStatus::InvalidProcessId), std::string("Process ID must be a positive integer"));
    CHECK_EQ(toString(static_cast<AllocationStatus>(99)), std::string("Unknown"));
    CHECK_EQ(toString(DeallocationStatus::Success), std::string("Success"));
    CHECK_EQ(toString(DeallocationStatus::ProcessNotFound), std::string("Process holds no allocation"));
    CHECK_EQ(toString(static_cast<DeallocationStatus>(99)), std::string("Unknown"));
}

void test_algorithm_names_and_unknown_fallbacks() {
    CHECK_EQ(toString(PlacementAlgorithm::FirstFit), std::string("First Fit"));
    CHECK_EQ(toString(PlacementAlgorithm::BestFit), std::string("Best Fit"));
    CHECK_EQ(toString(PlacementAlgorithm::WorstFit), std::string("Worst Fit"));
    CHECK_EQ(toString(static_cast<PlacementAlgorithm>(99)), std::string("Unknown"));
    CHECK_EQ(toString(ReplacementAlgorithm::FIFO), std::string("FIFO"));
    CHECK_EQ(toString(ReplacementAlgorithm::LRU), std::string("LRU"));
    CHECK_EQ(toString(ReplacementAlgorithm::Optimal), std::string("Optimal"));
    CHECK_EQ(toString(static_cast<ReplacementAlgorithm>(99)), std::string("Unknown"));
}

// ---------- direct API misuse ----------

void test_access_rejects_a_negative_page_even_after_valid_reset() {
    FIFO fifo;
    fifo.reset(2, {1, 2});
    CHECK_THROWS(fifo.access(-1), std::invalid_argument);
    // The failed call must not have consumed a position or corrupted state.
    const StepRecord first = fifo.access(1);
    CHECK_EQ(first.position, 0);
    CHECK(first.fault);
}

void test_rejected_reset_leaves_previous_run_usable() {
    FIFO fifo;
    fifo.reset(2, {1, 2, 3});
    fifo.access(1);
    CHECK_THROWS(fifo.reset(0, {1}), std::invalid_argument);  // rejected: frame count 0
    CHECK_THROWS(fifo.reset(2, {1, -1}), std::invalid_argument);  // rejected: negative page
    const StepRecord next = fifo.access(2);                    // run continues where it was
    CHECK_EQ(next.position, 1);
}

// ---------- arithmetic limits ----------

void test_validate_block_list_detects_end_address_overflow() {
    const Size max = std::numeric_limits<Size>::max();
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeFree(0, max), MemoryBlock::makeFree(max, 1)};
    const BlockListValidation result = validateBlockList(blocks, max);
    CHECK(!result.valid);
    CHECK(result.message.find("overflows") != std::string::npos);
}

void test_split_threshold_of_one_is_the_minimum() {
    AllocatorConfig config;
    config.splitThreshold = 1;
    MemoryAllocator allocator({10}, PlacementAlgorithm::FirstFit, config);
    CHECK(allocator.allocate(1, 9).succeeded());  // leftover of exactly 1 is still split off
    CHECK_EQ(allocator.blocks().size(), 2);
    CHECK_EQ(allocator.blocks()[1].size, 1);
}

// ---------- lookups that find nothing ----------

void test_find_returns_null_when_nothing_matches() {
    const AllocationComparison none;
    CHECK(none.find(PlacementAlgorithm::BestFit) == nullptr);
    const PageComparison noPages;
    CHECK(noPages.find("FIFO") == nullptr);
    const ProcessRegistry registry;
    CHECK(registry.find(1) == nullptr);
}

void test_page_comparison_has_no_optimal_baseline_without_optimal() {
    // A comparison lacking an Optimal result must still be constructible and searchable.
    PageComparison partial;
    partial.frameCount = 3;
    SimulationResult fifo;
    fifo.algorithm = "FIFO";
    partial.results.push_back(fifo);
    CHECK(partial.find("Optimal") == nullptr);
    CHECK(partial.find("FIFO") != nullptr);
}

}  // namespace

int main() {
    RUN_TEST(test_process_and_block_state_texts);
    RUN_TEST(test_registry_status_texts);
    RUN_TEST(test_allocation_status_texts);
    RUN_TEST(test_algorithm_names_and_unknown_fallbacks);

    RUN_TEST(test_access_rejects_a_negative_page_even_after_valid_reset);
    RUN_TEST(test_rejected_reset_leaves_previous_run_usable);

    RUN_TEST(test_validate_block_list_detects_end_address_overflow);
    RUN_TEST(test_split_threshold_of_one_is_the_minimum);

    RUN_TEST(test_find_returns_null_when_nothing_matches);
    RUN_TEST(test_page_comparison_has_no_optimal_baseline_without_optimal);

    return TEST_SUMMARY();
}
