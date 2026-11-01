// Tests for the CLI layer: InputValidator, TablePrinter, and complete scripted
// sessions run through CliApp with in-memory streams.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "CliApp.h"
#include "FIFO.h"
#include "InputValidator.h"
#include "PerformanceAnalyzer.h"
#include "TablePrinter.h"
#include "TestHarness.h"

using namespace memsim;

namespace {

// ---------- helpers ----------

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string lineContaining(const std::string& text, const std::string& needle) {
    for (const std::string& line : linesOf(text)) {
        if (contains(line, needle)) {
            return line;
        }
    }
    return "";
}

std::string lineStartingWith(const std::string& text, const std::string& prefix) {
    for (const std::string& line : linesOf(text)) {
        if (line.compare(0, prefix.size(), prefix) == 0) {
            return line;
        }
    }
    return "";
}

std::size_t countOf(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) {
        ++count;
    }
    return count;
}

std::string capture(const std::function<void(std::ostream&)>& print) {
    std::ostringstream out;
    print(out);
    return out.str();
}

struct Run {
    std::string output;
    int exitCode;
};

Run runScript(const std::string& input) {
    std::istringstream in(input);
    std::ostringstream out;
    CliApp app(in, out);
    const int code = app.run();
    return {out.str(), code};
}

#define CHECK_CONTAINS(text, needle) \
    ::testing::check(contains((text), (needle)), "output should contain: " needle, __FILE__, __LINE__)

// Memory-submenu "Initialize memory" with the textbook partitions, dynamic mode, default threshold.
const std::string kInit = "1\n100 500 200 300 600\n\n\n";
const std::string kTextbookRefs = "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1";

// Value of a parse that is expected to succeed. A failed parse is reported as a test
// failure and yields a default value instead of dereferencing an empty optional.
template <typename T>
T valueOf(const Parsed<T>& parsed) {
    CHECK(parsed.ok());
    return parsed.value.value_or(T{});
}

// ======================= InputValidator =======================

void test_trim() {
    CHECK_EQ(InputValidator::trim("  42 \t\r\n"), std::string("42"));
    CHECK_EQ(InputValidator::trim(""), std::string(""));
    CHECK_EQ(InputValidator::trim("   "), std::string(""));
}

void test_menu_choice() {
    CHECK_EQ(valueOf(InputValidator::parseMenuChoice("3", 1, 6)), 3);
    CHECK_EQ(valueOf(InputValidator::parseMenuChoice(" 6 ", 1, 6)), 6);
    for (const char* bad : {"0", "7", "abc", "", "-1", "1.5", "+2", "2x"}) {
        const auto parsed = InputValidator::parseMenuChoice(bad, 1, 6);
        CHECK(!parsed.ok());
        CHECK(contains(parsed.error, "from 1 to 6"));
    }
}

void test_process_id() {
    CHECK_EQ(valueOf(InputValidator::parseProcessId("5")), 5);
    CHECK(contains(InputValidator::parseProcessId("0").error, "greater than 0"));
    CHECK(contains(InputValidator::parseProcessId("-3").error, "negative"));
    CHECK(contains(InputValidator::parseProcessId("abc").error, "whole number"));
    CHECK(contains(InputValidator::parseProcessId("").error, "required"));
    CHECK(contains(InputValidator::parseProcessId("99999999999999999999").error, "too large"));
    CHECK(contains(InputValidator::parseProcessId("2000000000").error, "too large"));
}

void test_memory_size() {
    CHECK_EQ(valueOf(InputValidator::parseMemorySize("212")), 212);
    CHECK_EQ(valueOf(InputValidator::parseMemorySize("  7  ")), 7);
    CHECK_EQ(valueOf(InputValidator::parseMemorySize("1000000000")), 1000000000);
    CHECK(contains(InputValidator::parseMemorySize("0").error, "greater than 0"));
    CHECK(contains(InputValidator::parseMemorySize("-5").error, "negative"));
    CHECK(contains(InputValidator::parseMemorySize("12.5").error, "whole number"));
    CHECK(contains(InputValidator::parseMemorySize("1e3").error, "whole number"));
    CHECK(contains(InputValidator::parseMemorySize("10kb").error, "whole number"));
    CHECK(contains(InputValidator::parseMemorySize("1000000001").error, "too large"));
    CHECK(contains(InputValidator::parseMemorySize("0", "Process size").error, "Process size"));
}

void test_split_threshold_and_frame_count() {
    CHECK_EQ(valueOf(InputValidator::parseSplitThreshold("4")), 4);
    CHECK(!InputValidator::parseSplitThreshold("0").ok());
    CHECK_EQ(valueOf(InputValidator::parseFrameCount("1")), 1);
    CHECK_EQ(valueOf(InputValidator::parseFrameCount("1000")), 1000);
    CHECK(contains(InputValidator::parseFrameCount("0").error, "at least 1"));
    CHECK(contains(InputValidator::parseFrameCount("1001").error, "at most 1000"));
    CHECK(contains(InputValidator::parseFrameCount("-2").error, "negative"));
    CHECK(contains(InputValidator::parseFrameCount("x").error, "whole number"));
}

void test_block_sizes() {
    const auto parsed = InputValidator::parseBlockSizes("100 500, 200,300   600");
    CHECK(parsed.ok());
    CHECK(valueOf(parsed) == std::vector<Size>({100, 500, 200, 300, 600}));

    CHECK(contains(InputValidator::parseBlockSizes("").error, "At least one"));
    CHECK(contains(InputValidator::parseBlockSizes("   ,  ").error, "At least one"));
    const auto zero = InputValidator::parseBlockSizes("100 0 5");
    CHECK(!zero.ok());
    CHECK(contains(zero.error, "Block 2"));
    CHECK(contains(zero.error, "greater than 0"));
    CHECK(contains(InputValidator::parseBlockSizes("100 -5").error, "negative"));
    CHECK(contains(InputValidator::parseBlockSizes("10 abc").error, "Block 2"));

    std::string tooMany;
    for (int i = 0; i < 1001; ++i) {
        tooMany += "1 ";
    }
    CHECK(contains(InputValidator::parseBlockSizes(tooMany).error, "At most 1000"));
}

void test_page_references() {
    const auto parsed = InputValidator::parsePageReferences("7 0 1,2");
    CHECK(parsed.ok());
    CHECK(valueOf(parsed) == std::vector<PageId>({7, 0, 1, 2}));

    const auto empty = InputValidator::parsePageReferences("");
    CHECK(empty.ok());
    CHECK(valueOf(empty).empty());

    CHECK(contains(InputValidator::parsePageReferences("1 -2").error, "Reference 2"));
    CHECK(contains(InputValidator::parsePageReferences("1 -2").error, "negative"));
    CHECK(contains(InputValidator::parsePageReferences("1 x").error, "whole number"));
    CHECK(contains(InputValidator::parsePageReferences("1;2").error, "whole number"));
    CHECK(contains(InputValidator::parsePageReferences("3000000000").error, "too large"));
    CHECK((valueOf(InputValidator::parsePageReferences("0")))[0] == 0);  // page 0 is valid

    std::string tooLong;
    for (int i = 0; i < 10001; ++i) {
        tooLong += "1 ";
    }
    CHECK(contains(InputValidator::parsePageReferences(tooLong).error, "At most 10000"));
}

// ======================= TablePrinter =======================

void test_format_helpers() {
    CHECK_EQ(TablePrinter::formatPercent(0.25), std::string("25.00%"));
    CHECK_EQ(TablePrinter::formatPercent(0.0), std::string("0.00%"));
    CHECK_EQ(TablePrinter::formatPercent(1.0), std::string("100.00%"));
    CHECK_EQ(TablePrinter::formatRatio(0.5), std::string("0.500"));
    CHECK_EQ(TablePrinter::formatRatio(0.0), std::string("0.000"));
}

void test_memory_map_content_and_shape() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeAllocated(0, 100, 1, 60), MemoryBlock::makeFree(100, 50)};
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printMemoryMap(os, blocks); });
    const std::string allocatedRow = lineStartingWith(text, "| 0 ");
    CHECK(contains(allocatedRow, "Allocated"));
    CHECK(contains(allocatedRow, "P1"));
    CHECK(contains(allocatedRow, " 99 "));
    CHECK(endsWith(allocatedRow, "40 |"));  // 100 - 60 internal fragmentation
    const std::string freeRow = lineStartingWith(text, "| 1 ");
    CHECK(contains(freeRow, "Free"));
    CHECK(contains(freeRow, " 149 "));

    // Every table line has the same width.
    std::size_t width = 0;
    for (const std::string& line : linesOf(text)) {
        if (line.empty()) {
            continue;
        }
        width = width == 0 ? line.size() : width;
        CHECK_EQ(line.size(), width);
    }
}

void test_process_table() {
    ProcessRegistry registry;
    registry.add(1, 212);
    registry.add(12, 5);
    registry.setState(12, ProcessState::Failed);
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printProcessTable(os, registry); });
    CHECK(contains(lineStartingWith(text, "| P1 "), "Pending"));
    CHECK(contains(lineStartingWith(text, "| P1 "), "212"));
    CHECK(contains(lineStartingWith(text, "| P12"), "Failed"));
}

void test_fragmentation_report_and_zero_free() {
    const FragmentationReport zeroFree = FragmentationAnalyzer::analyze({MemoryBlock::makeAllocated(0, 100, 1, 80)});
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printFragmentation(os, zeroFree); });
    CHECK(contains(lineContaining(text, "External fragmentation indicator |"), "0.000"));
    CHECK(contains(lineStartingWith(text, "| Internal fragmentation"), "20"));
    CHECK_CONTAINS(text, "NOT internal fragmentation");  // internal vs external explained

    const FragmentationReport scattered = FragmentationAnalyzer::analyze(
        {MemoryBlock::makeFree(0, 100), MemoryBlock::makeAllocated(100, 50, 1, 50), MemoryBlock::makeFree(150, 100)});
    const std::string t2 = capture([&](std::ostream& os) { TablePrinter::printFragmentation(os, scattered); });
    CHECK(contains(lineContaining(t2, "External fragmentation indicator |"), "0.500"));
}

void test_allocation_stats_modes() {
    MemoryAllocator dynamic({100, 200}, PlacementAlgorithm::BestFit);
    dynamic.allocate(1, 50);
    const std::string t1 = capture([&](std::ostream& os) { TablePrinter::printAllocationStats(os, dynamic); });
    CHECK_CONTAINS(t1, "Algorithm: Best Fit");
    CHECK_CONTAINS(t1, "dynamic partitions (split threshold 1, merge on free)");
    CHECK(contains(lineContaining(t1, "Allocation success rate"), "100.00%"));

    MemoryAllocator fixed({100, 200}, PlacementAlgorithm::FirstFit, AllocatorConfig::fixedPartitions());
    const std::string t2 = capture([&](std::ostream& os) { TablePrinter::printAllocationStats(os, fixed); });
    CHECK_CONTAINS(t2, "fixed partitions (no split, no merge)");
    CHECK(contains(lineContaining(t2, "Allocation success rate"), "0.00%"));  // no attempts: no division by zero
}

void test_page_table_output() {
    FIFO fifo;
    const SimulationResult result = PageReplacementSimulator::run(fifo, 2, {1, 2, 1});
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printPageTable(os, result); });
    CHECK_CONTAINS(text, "FIFO with 2 frame(s)");
    CHECK(contains(lineStartingWith(text, "|    1 "), "[1 -]"));
    CHECK(contains(lineStartingWith(text, "|    1 "), "FAULT"));
    CHECK(contains(lineStartingWith(text, "|    2 "), "[1 2]"));
    CHECK(contains(lineStartingWith(text, "|    3 "), "HIT"));

    FIFO again;
    const SimulationResult bare = PageReplacementSimulator::run(again, 2, {1, 2, 1}, SimulationOptions{false});
    CHECK_CONTAINS(capture([&](std::ostream& os) { TablePrinter::printPageTable(os, bare); }), "(not recorded)");
}

void test_page_table_abbreviates_very_wide_frame_lists() {
    // 16 frames is shown in full; 20 is cut to the first 16 slots plus a count.
    FIFO exact;
    const SimulationResult sixteen = PageReplacementSimulator::run(exact, 16, {1, 2, 3});
    const std::string full = capture([&](std::ostream& os) { TablePrinter::printPageTable(os, sixteen); });
    CHECK(!contains(full, "more]"));
    CHECK(!contains(full, "Only the first"));

    FIFO wide;
    const SimulationResult twenty = PageReplacementSimulator::run(wide, 20, {1, 2, 3});
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printPageTable(os, twenty); });
    std::string expected = "[1";
    for (int i = 1; i < 16; ++i) {
        expected += " -";
    }
    expected += " ... +4 more]";
    CHECK(contains(lineStartingWith(text, "|    1 "), expected));
    CHECK_CONTAINS(text, "Only the first 16 of 20 frames are shown in each row.");
    CHECK_EQ(twenty.steps[0].frames.size(), 20);  // the data itself is not truncated

    // At the CLI maximum, no output line grows with the frame count.
    FIFO maximum;
    const SimulationResult thousand = PageReplacementSimulator::run(maximum, 1000, {1, 2, 3, 2, 1});
    const std::string big = capture([&](std::ostream& os) { TablePrinter::printPageTable(os, thousand); });
    std::size_t longest = 0;
    for (const std::string& line : linesOf(big)) {
        longest = std::max(longest, line.size());
    }
    CHECK(longest < 120);
}

void test_page_metrics_output() {
    FIFO fifo;
    const SimulationResult result = PageReplacementSimulator::run(fifo, 2, {1, 2, 1});
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printPageMetrics(os, result); });
    CHECK(contains(lineContaining(text, "Hit ratio"), "33.33%"));
    CHECK(contains(lineContaining(text, "Fault ratio"), "66.67%"));
    CHECK(contains(lineContaining(text, "Page faults"), "2"));
}

void test_page_comparison_output() {
    const PageComparison cmp = PageReplacementSimulator::compareAll(
        3, {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1}, SimulationOptions{false});
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printPageComparison(os, cmp); });
    const std::string fifo = lineStartingWith(text, "| FIFO");
    const std::string lru = lineStartingWith(text, "| LRU");
    const std::string optimal = lineStartingWith(text, "| Optimal");
    CHECK(contains(fifo, "15") && contains(fifo, "75.00%") && endsWith(fifo, "6 |"));
    CHECK(contains(lru, "12") && contains(lru, "40.00%") && endsWith(lru, "3 |"));
    CHECK(contains(optimal, "9") && contains(optimal, "55.00%") && endsWith(optimal, "0 |"));
    CHECK_CONTAINS(text, "this reference string and frame count only");
    CHECK_CONTAINS(text, "offline benchmark");
}

void test_allocation_comparison_output() {
    const AllocationComparison cmp = PerformanceAnalyzer::compareAllocation(
        {100, 500, 200, 300, 600},
        {AllocationEvent::allocate(1, 212), AllocationEvent::allocate(2, 417), AllocationEvent::allocate(3, 112),
         AllocationEvent::allocate(4, 426)});
    const std::string text = capture([&](std::ostream& os) { TablePrinter::printAllocationComparison(os, cmp); });
    CHECK_CONTAINS(text, "Events replayed: 4");
    const std::string header = lineStartingWith(text, "| Metric");
    CHECK(contains(header, "First Fit") && contains(header, "Best Fit") && contains(header, "Worst Fit"));
    const std::string rate = lineStartingWith(text, "| Allocation success rate");
    CHECK(contains(rate, "75.00%") && contains(rate, "100.00%"));
    CHECK_CONTAINS(text, "no algorithm is best in general");
}

// ======================= scripted CliApp sessions =======================

void test_cli_exit_and_eof() {
    const Run exit = runScript("6\n");
    CHECK_EQ(exit.exitCode, 0);
    CHECK_CONTAINS(exit.output, "Goodbye.");

    const Run empty = runScript("");  // end of input must terminate, not loop
    CHECK_EQ(empty.exitCode, 0);
    CHECK_CONTAINS(empty.output, "MAIN MENU");

    const Run crlf = runScript("6\r\n");
    CHECK_CONTAINS(crlf.output, "Goodbye.");
}

void test_cli_rejects_invalid_menu_choices() {
    const Run run = runScript("abc\n0\n7\n-1\n6\n");
    CHECK_EQ(countOf(run.output, "Invalid choice. Enter a number from 1 to 6."), 4);
    CHECK_CONTAINS(run.output, "Goodbye.");
}

void test_cli_eof_in_the_middle_of_a_prompt() {
    const Run run = runScript("1\n1\n100");  // input ends at the mode prompt
    CHECK_EQ(run.exitCode, 0);
    CHECK(!contains(run.output, "Memory initialized"));
}

void test_cli_allocate_and_show_memory_map() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n6\n8\n6\n");
    CHECK_CONTAINS(run.output, "Memory initialized: 5 block(s), total 1700 units");
    CHECK_CONTAINS(run.output, "P1 added (size 212, pending).");
    CHECK_CONTAINS(run.output, "P1 allocated at address 100 using First Fit");
    CHECK(contains(lineStartingWith(run.output, "| 1 |   100 |  311 |"), "Allocated"));
    CHECK(contains(lineStartingWith(run.output, "| 2 |   312 |  599 |"), "Free"));  // 500 - 212 remainder
    CHECK_CONTAINS(run.output, "Total 1700   Allocated 212   Free 1488");
}

void test_cli_add_process_validation() {
    const Run run = runScript("1\n" + kInit + "2\n-3\n0\nabc\n1\n-5\nx\n0\n50\n8\n6\n");
    CHECK_CONTAINS(run.output, "Process ID must not be negative");
    CHECK_CONTAINS(run.output, "Process ID must be greater than 0");
    CHECK_CONTAINS(run.output, "Process ID must be a whole number");
    CHECK_CONTAINS(run.output, "Process size must not be negative");
    CHECK_CONTAINS(run.output, "Process size must be a whole number");
    CHECK_CONTAINS(run.output, "Process size must be greater than 0");
    CHECK_CONTAINS(run.output, "P1 added (size 50, pending).");
}

void test_cli_rejects_duplicate_process_id() {
    const Run run = runScript("1\n" + kInit + "2\n1\n10\n2\n1\n8\n6\n");
    CHECK_CONTAINS(run.output, "A process with this ID already exists (P1)");
    CHECK_EQ(countOf(run.output, "added (size"), 1);
}

void test_cli_requires_initialized_memory() {
    const Run run = runScript("1\n2\n1\n10\n4\n5\n6\n7\n8\n3\n6\n");
    // option 2 (add) works without memory; 4, 5, 6, 7 and main-menu 3 do not
    CHECK_EQ(countOf(run.output, "Initialize memory first (option 1)."), 5);
}

void test_cli_allocate_with_no_processes() {
    const Run run = runScript("1\n" + kInit + "4\n8\n6\n");
    CHECK_CONTAINS(run.output, "No processes defined");
}

void test_cli_allocation_failure_reports_no_block() {
    const Run run = runScript("1\n1\n100\n\n\n2\n1\n200\n4\n1\n8\n6\n");
    CHECK_CONTAINS(run.output, "Allocation failed: no free block can hold 200 units.");
    CHECK(!contains(run.output, "not in one piece"));  // only 100 free in total
}

void test_cli_allocation_failure_explains_external_fragmentation() {
    const std::string script =
        "1\n1\n300\n\n\n"
        "2\n1\n100\n2\n2\n100\n2\n3\n100\n2\n4\n150\n"
        "4\n1\n4\n2\n4\n3\n"
        "5\n1\n5\n3\n"
        "4\n4\n"
        "8\n6\n";
    const Run run = runScript(script);
    CHECK_CONTAINS(run.output, "Allocation failed: no free block can hold 150 units.");
    CHECK_CONTAINS(run.output, "200 units are free in total, but not in one piece (external fragmentation)");
}

void test_cli_deallocate_flow() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n5\n1\n5\n1\n8\n6\n");
    CHECK_CONTAINS(run.output, "P1 deallocated (212 units freed).");
    CHECK_CONTAINS(run.output, "P1 holds no allocation.");
}

void test_cli_algorithm_selection_changes_placement() {
    const Run best = runScript("1\n" + kInit + "3\n2\n2\n1\n212\n4\n1\n8\n6\n");
    CHECK_CONTAINS(best.output, "Allocation algorithm: Best Fit");
    CHECK_CONTAINS(best.output, "P1 allocated at address 800 using Best Fit");

    const Run worst = runScript("1\n" + kInit + "3\n3\n2\n1\n212\n4\n1\n8\n6\n");
    CHECK_CONTAINS(worst.output, "P1 allocated at address 1100 using Worst Fit");
}

void test_cli_cannot_allocate_a_process_twice() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n4\n1\n8\n6\n");
    CHECK_CONTAINS(run.output, "P1 is already allocated.");
    CHECK_EQ(countOf(run.output, "allocated at address"), 1);  // the second attempt allocated nothing
}

void test_cli_algorithm_chosen_before_initialization_is_used() {
    // Best Fit is selected first, then memory is initialized: the new allocator must use it.
    const Run run = runScript("1\n3\n2\n" + kInit + "2\n1\n212\n4\n1\n8\n6\n");
    CHECK_CONTAINS(run.output, "P1 allocated at address 800 using Best Fit");
}

void test_cli_fixed_partition_mode_shows_internal_fragmentation() {
    const Run run = runScript("1\n1\n100 200\n2\n2\n1\n30\n4\n1\n8\n6\n");
    CHECK_CONTAINS(run.output, "P1 allocated at address 0");
    CHECK_CONTAINS(run.output, "internal fragmentation 70");
}

void test_cli_split_threshold_validation_and_internal_fragmentation() {
    const Run run = runScript("1\n1\n100\n1\n0\n-2\n30\n2\n1\n80\n4\n1\n8\n6\n");
    CHECK_CONTAINS(run.output, "Split threshold must be greater than 0");
    CHECK_CONTAINS(run.output, "Split threshold must not be negative");
    CHECK_CONTAINS(run.output, "internal fragmentation 20");  // leftover 20 < threshold 30
}

void test_cli_initialize_validation_and_cancel() {
    const Run run = runScript("1\n1\n100 0 5\n-4\n50\n\n\n8\n6\n");
    CHECK_CONTAINS(run.output, "Block 2: Block size must be greater than 0");
    CHECK_CONTAINS(run.output, "Block 1: Block size must not be negative");
    CHECK_CONTAINS(run.output, "Memory initialized: 1 block(s), total 50 units");

    const Run cancelled = runScript("1\n2\ncancel\n8\n6\n");
    CHECK_CONTAINS(cancelled.output, "Cancelled.");
    CHECK_CONTAINS(cancelled.output, "Goodbye.");
}

void test_cli_reinitialize_resets_session() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n" + kInit + "4\n8\n6\n");
    CHECK_CONTAINS(run.output, "Process list and history were reset.");
    CHECK_CONTAINS(run.output, "No processes defined");
}

void test_cli_statistics_and_fragmentation_analysis() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n7\n8\n3\n6\n");
    CHECK(contains(lineContaining(run.output, "Allocation success rate"), "100.00%"));
    CHECK(contains(lineStartingWith(run.output, "| Largest free block"), "600"));
    CHECK_CONTAINS(run.output, "=== FRAGMENTATION ANALYSIS ===");
    CHECK_CONTAINS(run.output, "External fragmentation indicator = 1 - largest free block / total free memory");
}

void test_cli_page_replacement_run() {
    const Run run = runScript("2\n1\n3\n2\n" + kTextbookRefs + "\n3\n2\n4\n5\n7\n6\n");
    CHECK_CONTAINS(run.output, "Frame count set to 3.");
    CHECK_CONTAINS(run.output, "Reference string set (20 reference(s)).");
    CHECK_CONTAINS(run.output, "Page replacement algorithm: LRU.");
    CHECK_CONTAINS(run.output, "LRU with 3 frame(s)");
    CHECK(contains(lineContaining(run.output, "Page faults"), "12"));
    CHECK(contains(lineContaining(run.output, "Page hits"), "8"));
    CHECK_EQ(countOf(run.output, "LRU with 3 frame(s)"), 2);  // shown on run and again on "display table"
}

void test_cli_page_replacement_input_errors() {
    const Run run = runScript("2\n4\n1\n0\n-2\n1001\nx\n3\n4\n7\n6\n");
    CHECK_CONTAINS(run.output, "Set the frame count first (option 1).");
    CHECK_CONTAINS(run.output, "Frame count must be at least 1");
    CHECK_CONTAINS(run.output, "Frame count must not be negative");
    CHECK_CONTAINS(run.output, "Frame count must be at most 1000");
    CHECK_CONTAINS(run.output, "Frame count must be a whole number");
    CHECK_CONTAINS(run.output, "Frame count set to 3.");
    CHECK_CONTAINS(run.output, "Enter the page reference string first (option 2).");
}

void test_cli_reference_string_errors_and_empty_string() {
    const Run bad = runScript("2\n2\n1 -2\n1 x\n3 4 5\n7\n6\n");
    CHECK_CONTAINS(bad.output, "Reference 2: Page number must not be negative");
    CHECK_CONTAINS(bad.output, "Reference 2: Page number must be a whole number");
    CHECK_CONTAINS(bad.output, "Reference string set (3 reference(s)).");

    const Run empty = runScript("2\n1\n2\n2\n\n4\n7\n6\n");
    CHECK_CONTAINS(empty.output, "Reference string set (0 reference(s)).");
    CHECK(contains(lineContaining(empty.output, "Total page references"), "0"));
    CHECK(contains(lineContaining(empty.output, "Hit ratio"), "0.00%"));
}

void test_cli_display_table_before_any_run() {
    const Run run = runScript("2\n5\n7\n6\n");
    CHECK_CONTAINS(run.output, "No simulation result yet");
}

void test_cli_page_comparison() {
    const Run run = runScript("2\n1\n3\n2\n" + kTextbookRefs + "\n6\n7\n6\n");
    CHECK(endsWith(lineStartingWith(run.output, "| FIFO"), "6 |"));
    CHECK(contains(lineStartingWith(run.output, "| LRU"), "12"));
    CHECK(contains(lineStartingWith(run.output, "| Optimal"), "9"));
}

void test_cli_comparisons_require_data() {
    const Run run = runScript("4\n2\n1\n3\n6\n");
    CHECK_CONTAINS(run.output, "Set the frame count and the reference string first");
    CHECK_CONTAINS(run.output, "Initialize memory first (option 1).");

    const Run noHistory = runScript("1\n" + kInit + "8\n4\n1\n3\n6\n");
    CHECK_CONTAINS(noHistory.output, "No allocation history yet");
}

void test_cli_allocation_comparison_replays_history() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n8\n4\n1\n3\n6\n");
    CHECK_CONTAINS(run.output, "Events replayed: 1");
    CHECK(contains(lineStartingWith(run.output, "| Metric"), "Worst Fit"));
    CHECK(contains(lineStartingWith(run.output, "| Allocation success rate"), "100.00%"));
}

void test_cli_sample_simulation() {
    const Run run = runScript("5\n6\n");
    CHECK_CONTAINS(run.output, "Part 1: contiguous allocation");
    CHECK_CONTAINS(run.output, "Part 2: page replacement");
    CHECK_CONTAINS(run.output, "Part 3: Belady's anomaly");
    const std::string rate = lineStartingWith(run.output, "| Allocation success rate");
    CHECK(contains(rate, "75.00%") && contains(rate, "100.00%"));
    CHECK_CONTAINS(run.output, "FIFO, 3 frames: 9 faults");
    CHECK_CONTAINS(run.output, "FIFO, 4 frames: 10 faults");
    CHECK_CONTAINS(run.output, "More frames produced MORE faults");
    CHECK(endsWith(lineStartingWith(run.output, "| Optimal"), "0 |"));
    CHECK_CONTAINS(run.output, "Sample simulation finished.");
}

void test_cli_sample_simulation_leaves_session_untouched() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n8\n5\n1\n6\n8\n6\n");
    CHECK_CONTAINS(run.output, "Total 1700   Allocated 212   Free 1488");  // P1 still allocated after the sample
}

void test_cli_history_includes_deallocations() {
    // allocate + deallocate = 2 events to replay
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n5\n1\n8\n4\n1\n3\n6\n");
    CHECK_CONTAINS(run.output, "Events replayed: 2");
}

void test_cli_reinitialize_clears_allocation_history() {
    const Run run = runScript("1\n" + kInit + "2\n1\n212\n4\n1\n" + kInit + "8\n4\n1\n3\n6\n");
    CHECK_CONTAINS(run.output, "No allocation history yet");
    CHECK(!contains(run.output, "Events replayed"));
}

void test_cli_failed_allocation_is_shown_as_failed_and_can_be_retried() {
    // P1 needs 200 but memory is 100: fails, shows as Failed in the process table, stays retryable.
    const Run run = runScript("1\n1\n100\n\n\n2\n1\n200\n4\n1\n4\n1\n8\n6\n");
    CHECK_EQ(countOf(run.output, "Allocation failed: no free block can hold 200 units."), 2);
    CHECK_CONTAINS(run.output, "| Failed");
}

// ---------- robustness: random input must never crash, hang or escape as an exception ----------

void test_cli_survives_random_input() {
    const std::vector<std::string> pool = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-1", "-5", "abc", "cancel", "CANCEL", "", " ",
        "100", "212", "10 20 30", "100 500 200 300 600", "7 0 1 2 0 3 0 4", "1,2,,3", "1 -2", "x y",
        "99999999999", "1000000000", "1001", "300", "50", "1.5", "+3", "0x10", "\t", "   2   "};
    std::uint32_t state = 20260101u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state >> 16;
    };

    for (int script = 0; script < 400; ++script) {
        std::string input;
        const std::uint32_t lines = 10 + next() % 90;
        for (std::uint32_t i = 0; i < lines; ++i) {
            input += pool[next() % pool.size()] + "\n";
        }
        const Run run = runScript(input);  // must return (no hang) after the input is exhausted
        CHECK_EQ(run.exitCode, 0);
        CHECK(contains(run.output, "MAIN MENU"));
    }
}

void test_cli_survives_binary_garbage_and_huge_lines() {
    std::string garbage;
    for (int i = 0; i < 4000; ++i) {
        garbage.push_back(static_cast<char>(i * 37 % 256));
        if (i % 61 == 0) {
            garbage.push_back('\n');
        }
    }
    CHECK_EQ(runScript(garbage).exitCode, 0);
    CHECK_EQ(runScript("1\n1\n" + std::string(200000, '9') + "\n6\n").exitCode, 0);       // 200k-digit number
    CHECK_EQ(runScript("2\n2\n" + std::string(100000, ' ') + "\ncancel\n7\n6\n").exitCode, 0);
    std::string manyTokens = "2\n2\n";
    for (int i = 0; i < 20000; ++i) {
        manyTokens += "1 ";
    }
    const Run tooMany = runScript(manyTokens + "\ncancel\n7\n6\n");
    CHECK_CONTAINS(tooMany.output, "At most 10000 references are allowed");
}

}  // namespace

int main() {
    RUN_TEST(test_trim);
    RUN_TEST(test_menu_choice);
    RUN_TEST(test_process_id);
    RUN_TEST(test_memory_size);
    RUN_TEST(test_split_threshold_and_frame_count);
    RUN_TEST(test_block_sizes);
    RUN_TEST(test_page_references);

    RUN_TEST(test_format_helpers);
    RUN_TEST(test_memory_map_content_and_shape);
    RUN_TEST(test_process_table);
    RUN_TEST(test_fragmentation_report_and_zero_free);
    RUN_TEST(test_allocation_stats_modes);
    RUN_TEST(test_page_table_output);
    RUN_TEST(test_page_table_abbreviates_very_wide_frame_lists);
    RUN_TEST(test_page_metrics_output);
    RUN_TEST(test_page_comparison_output);
    RUN_TEST(test_allocation_comparison_output);

    RUN_TEST(test_cli_exit_and_eof);
    RUN_TEST(test_cli_rejects_invalid_menu_choices);
    RUN_TEST(test_cli_eof_in_the_middle_of_a_prompt);
    RUN_TEST(test_cli_allocate_and_show_memory_map);
    RUN_TEST(test_cli_add_process_validation);
    RUN_TEST(test_cli_rejects_duplicate_process_id);
    RUN_TEST(test_cli_requires_initialized_memory);
    RUN_TEST(test_cli_allocate_with_no_processes);
    RUN_TEST(test_cli_allocation_failure_reports_no_block);
    RUN_TEST(test_cli_allocation_failure_explains_external_fragmentation);
    RUN_TEST(test_cli_deallocate_flow);
    RUN_TEST(test_cli_algorithm_selection_changes_placement);
    RUN_TEST(test_cli_cannot_allocate_a_process_twice);
    RUN_TEST(test_cli_algorithm_chosen_before_initialization_is_used);
    RUN_TEST(test_cli_fixed_partition_mode_shows_internal_fragmentation);
    RUN_TEST(test_cli_split_threshold_validation_and_internal_fragmentation);
    RUN_TEST(test_cli_initialize_validation_and_cancel);
    RUN_TEST(test_cli_reinitialize_resets_session);
    RUN_TEST(test_cli_statistics_and_fragmentation_analysis);
    RUN_TEST(test_cli_page_replacement_run);
    RUN_TEST(test_cli_page_replacement_input_errors);
    RUN_TEST(test_cli_reference_string_errors_and_empty_string);
    RUN_TEST(test_cli_display_table_before_any_run);
    RUN_TEST(test_cli_page_comparison);
    RUN_TEST(test_cli_comparisons_require_data);
    RUN_TEST(test_cli_allocation_comparison_replays_history);
    RUN_TEST(test_cli_sample_simulation);
    RUN_TEST(test_cli_sample_simulation_leaves_session_untouched);

    RUN_TEST(test_cli_history_includes_deallocations);
    RUN_TEST(test_cli_reinitialize_clears_allocation_history);
    RUN_TEST(test_cli_failed_allocation_is_shown_as_failed_and_can_be_retried);

    RUN_TEST(test_cli_survives_random_input);
    RUN_TEST(test_cli_survives_binary_garbage_and_huge_lines);

    return TEST_SUMMARY();
}
