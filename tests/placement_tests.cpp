// Placement-strategy selection tests (First Fit, Best Fit, Worst Fit).
// Expected indices below are derived by hand from the stated block sizes.

#include <optional>
#include <string>
#include <vector>

#include "BestFit.h"
#include "FirstFit.h"
#include "MemoryBlock.h"
#include "PlacementStrategy.h"
#include "TestHarness.h"
#include "WorstFit.h"

using namespace memsim;

namespace {

const std::optional<Size> kNone{};

std::optional<Size> at(Size index) { return index; }

void expectSelection(const PlacementStrategy& strategy, const std::vector<MemoryBlock>& blocks,
                     Size request, const std::optional<Size>& expected, int line) {
    const std::optional<Size> got = strategy.selectBlock(blocks, request);
    const auto show = [](const std::optional<Size>& v) {
        return v.has_value() ? std::to_string(*v) : std::string("none");
    };
    const std::string description = strategy.name() + " request " + std::to_string(request) +
                                    ": expected " + show(expected) + ", got " + show(got) +
                                    " (line " + std::to_string(line) + ")";
    ::testing::check(got == expected, description.c_str(), __FILE__, line);
}

#define EXPECT_SELECT(strategy, blocks, request, expected) \
    expectSelection((strategy), (blocks), (request), (expected), __LINE__)

// Classic textbook partition set.
std::vector<MemoryBlock> textbookBlocks() { return makeInitialBlocks({100, 500, 200, 300, 600}); }

// ---------- Known results on {100, 500, 200, 300, 600} ----------

void test_first_fit_textbook() {
    const FirstFit s;
    const auto b = textbookBlocks();
    EXPECT_SELECT(s, b, 212, at(1));  // 500 is first block >= 212
    EXPECT_SELECT(s, b, 417, at(1));
    EXPECT_SELECT(s, b, 112, at(1));
    EXPECT_SELECT(s, b, 426, at(1));
    EXPECT_SELECT(s, b, 100, at(0));  // first block, exact fit
    EXPECT_SELECT(s, b, 550, at(4));  // only 600 is large enough
}

void test_best_fit_textbook() {
    const BestFit s;
    const auto b = textbookBlocks();
    EXPECT_SELECT(s, b, 212, at(3));  // 300 is the smallest block >= 212
    EXPECT_SELECT(s, b, 417, at(1));  // 500
    EXPECT_SELECT(s, b, 112, at(2));  // 200
    EXPECT_SELECT(s, b, 426, at(1));  // 500
    EXPECT_SELECT(s, b, 100, at(0));  // exact fit
    EXPECT_SELECT(s, b, 550, at(4));
}

void test_worst_fit_textbook() {
    const WorstFit s;
    const auto b = textbookBlocks();
    EXPECT_SELECT(s, b, 212, at(4));  // 600 is the largest block
    EXPECT_SELECT(s, b, 417, at(4));
    EXPECT_SELECT(s, b, 112, at(4));
    EXPECT_SELECT(s, b, 100, at(4));  // largest even for a tiny request
    EXPECT_SELECT(s, b, 600, at(4));
}

// ---------- Exact size, no suitable block ----------

void test_exact_size_request_is_suitable() {
    const auto b = makeInitialBlocks({100, 50});
    EXPECT_SELECT(FirstFit{}, b, 50, at(0));  // first block (100) is already large enough
    EXPECT_SELECT(BestFit{}, b, 50, at(1));   // exact fit beats the larger block
    EXPECT_SELECT(WorstFit{}, b, 50, at(0));  // largest suitable is the 100 block
}

void test_no_suitable_block() {
    const auto b = textbookBlocks();
    EXPECT_SELECT(FirstFit{}, b, 601, kNone);
    EXPECT_SELECT(BestFit{}, b, 601, kNone);
    EXPECT_SELECT(WorstFit{}, b, 601, kNone);
}

void test_zero_size_request_selects_nothing() {
    const auto b = textbookBlocks();
    EXPECT_SELECT(FirstFit{}, b, 0, kNone);
    EXPECT_SELECT(BestFit{}, b, 0, kNone);
    EXPECT_SELECT(WorstFit{}, b, 0, kNone);
}

void test_empty_block_list() {
    const std::vector<MemoryBlock> none;
    EXPECT_SELECT(FirstFit{}, none, 10, kNone);
    EXPECT_SELECT(BestFit{}, none, 10, kNone);
    EXPECT_SELECT(WorstFit{}, none, 10, kNone);
}

// ---------- Allocated blocks are never candidates ----------

void test_allocated_blocks_are_skipped() {
    const std::vector<MemoryBlock> b = {
        MemoryBlock::makeAllocated(0, 500, 1, 500),  // big but taken
        MemoryBlock::makeFree(500, 100),
        MemoryBlock::makeFree(600, 300),
    };
    EXPECT_SELECT(FirstFit{}, b, 50, at(1));
    EXPECT_SELECT(BestFit{}, b, 50, at(1));
    EXPECT_SELECT(WorstFit{}, b, 50, at(2));  // 300, not the allocated 500
    EXPECT_SELECT(FirstFit{}, b, 400, kNone);  // only free space is 100 and 300
}

void test_all_blocks_allocated() {
    const std::vector<MemoryBlock> b = {MemoryBlock::makeAllocated(0, 100, 1, 100),
                                        MemoryBlock::makeAllocated(100, 100, 2, 50)};
    EXPECT_SELECT(FirstFit{}, b, 1, kNone);
    EXPECT_SELECT(BestFit{}, b, 1, kNone);
    EXPECT_SELECT(WorstFit{}, b, 1, kNone);
}

// ---------- Tie-breaking: lowest address wins ----------

void test_best_fit_tie_prefers_lowest_address() {
    const auto b = makeInitialBlocks({200, 150, 200, 150});
    EXPECT_SELECT(BestFit{}, b, 120, at(1));  // two 150s; index 1 comes first
}

void test_worst_fit_tie_prefers_lowest_address() {
    const auto b = makeInitialBlocks({200, 150, 200, 150});
    EXPECT_SELECT(WorstFit{}, b, 120, at(0));  // two 200s; index 0 comes first
}

void test_best_fit_exact_match_beats_larger_earlier_block() {
    const auto b = makeInitialBlocks({50, 200, 200, 100});
    EXPECT_SELECT(FirstFit{}, b, 100, at(1));
    EXPECT_SELECT(BestFit{}, b, 100, at(3));  // exact fit later in memory
}

// ---------- Strategies disagree on the same input ----------

void test_strategies_choose_differently() {
    const auto b = makeInitialBlocks({300, 120, 500});
    EXPECT_SELECT(FirstFit{}, b, 100, at(0));
    EXPECT_SELECT(BestFit{}, b, 100, at(1));
    EXPECT_SELECT(WorstFit{}, b, 100, at(2));
}

// ---------- Purity, factory, polymorphism ----------

void test_selection_is_const_and_repeatable() {
    const auto b = textbookBlocks();
    const BestFit s;
    const auto first = s.selectBlock(b, 212);
    const auto second = s.selectBlock(b, 212);
    CHECK(first == second);
    CHECK_EQ(b.size(), 5);
    for (const auto& block : b) {
        CHECK(block.isFree());  // input untouched
    }
}

void test_factory_creates_matching_strategy() {
    const auto b = textbookBlocks();
    const auto first = makePlacementStrategy(PlacementAlgorithm::FirstFit);
    const auto best = makePlacementStrategy(PlacementAlgorithm::BestFit);
    const auto worst = makePlacementStrategy(PlacementAlgorithm::WorstFit);

    CHECK(first != nullptr && best != nullptr && worst != nullptr);
    CHECK(first->name() == "First Fit");
    CHECK(best->name() == "Best Fit");
    CHECK(worst->name() == "Worst Fit");
    EXPECT_SELECT(*first, b, 212, at(1));
    EXPECT_SELECT(*best, b, 212, at(3));
    EXPECT_SELECT(*worst, b, 212, at(4));
}

void test_factory_rejects_invalid_enum() {
    CHECK_THROWS(makePlacementStrategy(static_cast<PlacementAlgorithm>(99)), std::invalid_argument);
}

void test_algorithm_to_string() {
    CHECK(toString(PlacementAlgorithm::FirstFit) == "First Fit");
    CHECK(toString(PlacementAlgorithm::BestFit) == "Best Fit");
    CHECK(toString(PlacementAlgorithm::WorstFit) == "Worst Fit");
}

}  // namespace

int main() {
    RUN_TEST(test_first_fit_textbook);
    RUN_TEST(test_best_fit_textbook);
    RUN_TEST(test_worst_fit_textbook);

    RUN_TEST(test_exact_size_request_is_suitable);
    RUN_TEST(test_no_suitable_block);
    RUN_TEST(test_zero_size_request_selects_nothing);
    RUN_TEST(test_empty_block_list);

    RUN_TEST(test_allocated_blocks_are_skipped);
    RUN_TEST(test_all_blocks_allocated);

    RUN_TEST(test_best_fit_tie_prefers_lowest_address);
    RUN_TEST(test_worst_fit_tie_prefers_lowest_address);
    RUN_TEST(test_best_fit_exact_match_beats_larger_earlier_block);
    RUN_TEST(test_strategies_choose_differently);

    RUN_TEST(test_selection_is_const_and_repeatable);
    RUN_TEST(test_factory_creates_matching_strategy);
    RUN_TEST(test_factory_rejects_invalid_enum);
    RUN_TEST(test_algorithm_to_string);

    return TEST_SUMMARY();
}
