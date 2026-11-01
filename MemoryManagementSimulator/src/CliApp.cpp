#include "CliApp.h"

#include <algorithm>
#include <cctype>
#include <exception>

#include "FragmentationAnalyzer.h"
#include "InputValidator.h"
#include "PerformanceAnalyzer.h"
#include "TablePrinter.h"

namespace memsim {

namespace {

bool isCancel(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "cancel";
}

std::string label(ProcessId id) { return "P" + std::to_string(id); }

const MemoryBlock* findOwnerBlock(const std::vector<MemoryBlock>& blocks, ProcessId id) {
    for (const MemoryBlock& block : blocks) {
        if (block.owner.has_value() && *block.owner == id) {
            return &block;
        }
    }
    return nullptr;
}

}  // namespace

CliApp::CliApp(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

// ---------------------------------------------------------------- I/O helpers

std::optional<std::string> CliApp::readLine(const std::string& prompt) {
    if (eof_) {
        return std::nullopt;
    }
    out_ << prompt << std::flush;
    std::string line;
    if (!std::getline(in_, line)) {
        eof_ = true;
        out_ << "\n";
        return std::nullopt;
    }
    return line;
}

std::optional<Size> CliApp::askChoice(Size minChoice, Size maxChoice) {
    while (true) {
        const std::optional<std::string> line = readLine("Choice: ");
        if (!line.has_value()) {
            return std::nullopt;
        }
        const Parsed<Size> parsed = InputValidator::parseMenuChoice(*line, minChoice, maxChoice);
        if (parsed.ok()) {
            return parsed.value;
        }
        out_ << "  " << parsed.error << ".\n";
    }
}

template <typename T, typename Parser>
std::optional<T> CliApp::ask(const std::string& prompt, Parser parse) {
    while (true) {
        const std::optional<std::string> line = readLine(prompt);
        if (!line.has_value()) {
            return std::nullopt;
        }
        const std::string text = InputValidator::trim(*line);
        if (isCancel(text)) {
            out_ << "  Cancelled.\n";
            return std::nullopt;
        }
        const Parsed<T> parsed = parse(text);
        if (parsed.ok()) {
            return parsed.value;
        }
        out_ << "  Error: " << parsed.error << ". (Type 'cancel' to go back.)\n";
    }
}

// ---------------------------------------------------------------------- main

int CliApp::run() {
    out_ << "Memory Management Simulator\n";
    while (!eof_) {
        printMainMenu();
        const std::optional<Size> choice = askChoice(1, 6);
        if (!choice.has_value()) {
            break;
        }
        try {
            switch (*choice) {
                case 1: memoryMenu(); break;
                case 2: pageMenu(); break;
                case 3: fragmentationAnalysis(); break;
                case 4: performanceMenu(); break;
                case 5: runSampleSimulation(); break;
                default: out_ << "Goodbye.\n"; return 0;
            }
        } catch (const std::exception& e) {
            out_ << "  Error: " << e.what() << "\n";
        }
    }
    return 0;
}

void CliApp::printMainMenu() {
    out_ << "\n=== MAIN MENU ===\n"
         << "  1. Memory Allocation\n"
         << "  2. Page Replacement\n"
         << "  3. Fragmentation Analysis\n"
         << "  4. Performance Comparison\n"
         << "  5. Run Sample Simulation\n"
         << "  6. Exit\n";
}

// ------------------------------------------------------------ memory allocation

void CliApp::memoryMenu() {
    while (!eof_) {
        out_ << "\n=== MEMORY ALLOCATION ===  (algorithm: " << toString(placement_) << ", memory: "
             << (allocator_.has_value() ? "initialized" : "not initialized") << ")\n"
             << "  1. Initialize memory\n"
             << "  2. Add process\n"
             << "  3. Select allocation algorithm\n"
             << "  4. Allocate process\n"
             << "  5. Deallocate process\n"
             << "  6. Display memory map\n"
             << "  7. Display allocation statistics\n"
             << "  8. Back\n";
        const std::optional<Size> choice = askChoice(1, 8);
        if (!choice.has_value() || *choice == 8) {
            return;
        }
        try {
            switch (*choice) {
                case 1: initializeMemory(); break;
                case 2: addProcess(); break;
                case 3: selectPlacementAlgorithm(); break;
                case 4: allocateProcess(); break;
                case 5: deallocateProcess(); break;
                case 6: displayMemoryMap(); break;
                default: displayAllocationStats(); break;
            }
        } catch (const std::exception& e) {
            out_ << "  Error: " << e.what() << "\n";
        }
    }
}

bool CliApp::requireAllocator() {
    if (!allocator_.has_value()) {
        out_ << "  Initialize memory first (option 1).\n";
        return false;
    }
    return true;
}

void CliApp::initializeMemory() {
    const auto sizes = ask<std::vector<Size>>(
        "Block sizes, separated by spaces or commas (e.g. 100 500 200 300 600): ",
        [](const std::string& text) { return InputValidator::parseBlockSizes(text); });
    if (!sizes.has_value()) {
        return;
    }
    const auto mode = ask<Size>("Mode - 1) dynamic partitions (split and merge)  2) fixed partitions [1]: ",
                                [](const std::string& text) {
                                    return text.empty() ? Parsed<Size>::success(1)
                                                        : InputValidator::parseMenuChoice(text, 1, 2);
                                });
    if (!mode.has_value()) {
        return;
    }
    AllocatorConfig config = AllocatorConfig::fixedPartitions();
    if (*mode == 1) {
        const auto threshold = ask<Size>(
            "Split threshold - smallest leftover that becomes a free block [1]: ", [](const std::string& text) {
                return text.empty() ? Parsed<Size>::success(1) : InputValidator::parseSplitThreshold(text);
            });
        if (!threshold.has_value()) {
            return;
        }
        config = AllocatorConfig{*threshold, true};
    }

    // Build first so a failure leaves the previous memory untouched.
    MemoryAllocator fresh(*sizes, placement_, config);
    allocator_.emplace(std::move(fresh));
    blockSizes_ = *sizes;
    config_ = config;
    registry_.clear();
    history_.clear();

    out_ << "Memory initialized: " << sizes->size() << " block(s), total " << allocator_->totalMemory()
         << " units. Process list and history were reset.\n";
    TablePrinter::printMemoryMap(out_, allocator_->blocks());
}

void CliApp::addProcess() {
    const auto id = ask<ProcessId>("Process ID (positive whole number): ",
                                   [](const std::string& text) { return InputValidator::parseProcessId(text); });
    if (!id.has_value()) {
        return;
    }
    if (registry_.find(*id) != nullptr) {
        out_ << "  Error: " << toString(RegistryStatus::DuplicateId) << " (" << label(*id) << ").\n";
        return;
    }
    const auto size = ask<Size>("Memory required by " + label(*id) + ": ", [](const std::string& text) {
        return InputValidator::parseMemorySize(text, "Process size");
    });
    if (!size.has_value()) {
        return;
    }
    const RegistryStatus status = registry_.add(*id, *size);
    if (status != RegistryStatus::Success) {
        out_ << "  Error: " << toString(status) << ".\n";
        return;
    }
    out_ << label(*id) << " added (size " << *size << ", pending).\n";
}

void CliApp::selectPlacementAlgorithm() {
    out_ << "  1. First Fit   2. Best Fit   3. Worst Fit\n";
    const std::optional<Size> choice = askChoice(1, 3);
    if (!choice.has_value()) {
        return;
    }
    placement_ = *choice == 1   ? PlacementAlgorithm::FirstFit
                 : *choice == 2 ? PlacementAlgorithm::BestFit
                                : PlacementAlgorithm::WorstFit;
    if (allocator_.has_value()) {
        allocator_->setAlgorithm(placement_);
    }
    out_ << "Allocation algorithm: " << toString(placement_) << " (applies to later allocations).\n";
}

void CliApp::allocateProcess() {
    if (!requireAllocator()) {
        return;
    }
    if (registry_.count() == 0) {
        out_ << "  No processes defined. Add a process first (option 2).\n";
        return;
    }
    TablePrinter::printProcessTable(out_, registry_);
    const auto id = ask<ProcessId>("Process ID to allocate: ",
                                   [](const std::string& text) { return InputValidator::parseProcessId(text); });
    if (!id.has_value()) {
        return;
    }
    const Process* process = registry_.find(*id);
    if (process == nullptr) {
        out_ << "  Error: " << toString(RegistryStatus::NotFound) << " (" << label(*id) << ").\n";
        return;
    }
    if (process->state == ProcessState::Allocated) {
        out_ << "  Error: " << label(*id) << " is already allocated.\n";
        return;
    }

    const Size size = process->size;
    const AllocationResult result = allocator_->allocate(*id, size);
    history_.push_back(AllocationEvent::allocate(*id, size));

    if (result.succeeded()) {
        registry_.setState(*id, ProcessState::Allocated);
        const MemoryBlock* block = findOwnerBlock(allocator_->blocks(), *id);
        out_ << label(*id) << " allocated at address " << *result.blockStart << " using " << toString(placement_)
             << " (block size " << block->size << ", internal fragmentation " << block->internalFragmentation()
             << ").\n";
        return;
    }
    registry_.setState(*id, ProcessState::Failed);
    if (result.status == AllocationStatus::NoSuitableBlock) {
        const FragmentationReport report = FragmentationAnalyzer::analyze(allocator_->blocks());
        out_ << "  Allocation failed: no free block can hold " << size << " units.\n";
        if (report.freeMemory >= size) {
            out_ << "  " << report.freeMemory << " units are free in total, but not in one piece"
                 << " (external fragmentation).\n";
        }
    } else {
        out_ << "  Allocation failed: " << toString(result.status) << ".\n";
    }
}

void CliApp::deallocateProcess() {
    if (!requireAllocator()) {
        return;
    }
    const auto id = ask<ProcessId>("Process ID to deallocate: ",
                                   [](const std::string& text) { return InputValidator::parseProcessId(text); });
    if (!id.has_value()) {
        return;
    }
    const Process* process = registry_.find(*id);
    if (process == nullptr || process->state != ProcessState::Allocated) {
        out_ << "  Error: " << label(*id) << " holds no allocation.\n";
        return;
    }
    const MemoryBlock* block = findOwnerBlock(allocator_->blocks(), *id);
    const Size freed = block != nullptr ? block->size : 0;

    const DeallocationStatus status = allocator_->deallocate(*id);
    history_.push_back(AllocationEvent::deallocate(*id));
    if (status != DeallocationStatus::Success) {
        out_ << "  Error: " << toString(status) << ".\n";
        return;
    }
    registry_.remove(*id);
    out_ << label(*id) << " deallocated (" << freed << " units freed).\n";
}

void CliApp::displayMemoryMap() {
    if (!requireAllocator()) {
        return;
    }
    TablePrinter::printMemoryMap(out_, allocator_->blocks());
    const FragmentationReport report = FragmentationAnalyzer::analyze(allocator_->blocks());
    out_ << "Total " << report.totalMemory << "   Allocated " << report.allocatedMemory << "   Free "
         << report.freeMemory << "\n";
}

void CliApp::displayAllocationStats() {
    if (!requireAllocator()) {
        return;
    }
    TablePrinter::printAllocationStats(out_, *allocator_);
}

void CliApp::fragmentationAnalysis() {
    out_ << "\n=== FRAGMENTATION ANALYSIS ===\n";
    if (!requireAllocator()) {
        return;
    }
    TablePrinter::printMemoryMap(out_, allocator_->blocks());
    TablePrinter::printFragmentation(out_, FragmentationAnalyzer::analyze(allocator_->blocks()));
}

// ------------------------------------------------------------ page replacement

void CliApp::pageMenu() {
    while (!eof_) {
        out_ << "\n=== PAGE REPLACEMENT ===  (algorithm: " << toString(replacement_) << ", frames: "
             << (frameCountSet_ ? std::to_string(frameCount_) : "not set") << ", references: "
             << (referencesSet_ ? std::to_string(references_.size()) : "not set") << ")\n"
             << "  1. Configure frame count\n"
             << "  2. Enter page reference string\n"
             << "  3. Select algorithm\n"
             << "  4. Run simulation\n"
             << "  5. Display page replacement table\n"
             << "  6. Compare FIFO, LRU and Optimal\n"
             << "  7. Back\n";
        const std::optional<Size> choice = askChoice(1, 7);
        if (!choice.has_value() || *choice == 7) {
            return;
        }
        try {
            switch (*choice) {
                case 1: configureFrameCount(); break;
                case 2: enterReferenceString(); break;
                case 3: selectReplacementAlgorithm(); break;
                case 4: runPageSimulation(); break;
                case 5: displayPageTable(); break;
                default: comparePageAlgorithms(); break;
            }
        } catch (const std::exception& e) {
            out_ << "  Error: " << e.what() << "\n";
        }
    }
}

void CliApp::configureFrameCount() {
    const auto frames = ask<Size>("Number of frames (1-" + std::to_string(InputLimits::kMaxFrames) + "): ",
                                  [](const std::string& text) { return InputValidator::parseFrameCount(text); });
    if (!frames.has_value()) {
        return;
    }
    frameCount_ = *frames;
    frameCountSet_ = true;
    lastResult_.reset();
    out_ << "Frame count set to " << frameCount_ << ".\n";
}

void CliApp::enterReferenceString() {
    const auto pages = ask<std::vector<PageId>>(
        "Page reference string, numbers separated by spaces or commas (blank = empty string): ",
        [](const std::string& text) { return InputValidator::parsePageReferences(text); });
    if (!pages.has_value()) {
        return;
    }
    references_ = *pages;
    referencesSet_ = true;
    lastResult_.reset();
    out_ << "Reference string set (" << references_.size() << " reference(s)).\n";
}

void CliApp::selectReplacementAlgorithm() {
    out_ << "  1. FIFO   2. LRU   3. Optimal\n";
    const std::optional<Size> choice = askChoice(1, 3);
    if (!choice.has_value()) {
        return;
    }
    replacement_ = *choice == 1   ? ReplacementAlgorithm::FIFO
                   : *choice == 2 ? ReplacementAlgorithm::LRU
                                  : ReplacementAlgorithm::Optimal;
    out_ << "Page replacement algorithm: " << toString(replacement_) << ".\n";
}

void CliApp::runPageSimulation() {
    if (!frameCountSet_) {
        out_ << "  Set the frame count first (option 1).\n";
        return;
    }
    if (!referencesSet_) {
        out_ << "  Enter the page reference string first (option 2).\n";
        return;
    }
    const std::unique_ptr<PageReplacementAlgorithm> algorithm = makeReplacementAlgorithm(replacement_);
    lastResult_ = PageReplacementSimulator::run(*algorithm, frameCount_, references_);
    TablePrinter::printPageTable(out_, *lastResult_);
    TablePrinter::printPageMetrics(out_, *lastResult_);
}

void CliApp::displayPageTable() {
    if (!lastResult_.has_value()) {
        out_ << "  No simulation result yet. Run a simulation first (option 4).\n";
        return;
    }
    TablePrinter::printPageTable(out_, *lastResult_);
    TablePrinter::printPageMetrics(out_, *lastResult_);
}

void CliApp::comparePageAlgorithms() {
    if (!frameCountSet_ || !referencesSet_) {
        out_ << "  Set the frame count and the reference string first (options 1 and 2).\n";
        return;
    }
    TablePrinter::printPageComparison(
        out_, PerformanceAnalyzer::comparePageReplacement(frameCount_, references_));
}

// ------------------------------------------------------- performance comparison

void CliApp::performanceMenu() {
    while (!eof_) {
        out_ << "\n=== PERFORMANCE COMPARISON ===\n"
             << "  1. Compare First, Best and Worst Fit (replays your allocation history)\n"
             << "  2. Compare FIFO, LRU and Optimal (uses your frame count and reference string)\n"
             << "  3. Back\n";
        const std::optional<Size> choice = askChoice(1, 3);
        if (!choice.has_value() || *choice == 3) {
            return;
        }
        try {
            if (*choice == 1) {
                compareAllocationAlgorithms();
            } else {
                comparePageAlgorithms();
            }
        } catch (const std::exception& e) {
            out_ << "  Error: " << e.what() << "\n";
        }
    }
}

void CliApp::compareAllocationAlgorithms() {
    if (!requireAllocator()) {
        return;
    }
    if (history_.empty()) {
        out_ << "  No allocation history yet. Allocate or deallocate some processes first.\n";
        return;
    }
    TablePrinter::printAllocationComparison(
        out_, PerformanceAnalyzer::compareAllocation(blockSizes_, history_, config_));
}

// ------------------------------------------------------------------ sample run

void CliApp::runSampleSimulation() {
    out_ << "\n=== SAMPLE SIMULATION ===\n";

    // Part 1: textbook partitions and requests.
    const std::vector<Size> blocks = {100, 500, 200, 300, 600};
    const Workload workload = {AllocationEvent::allocate(1, 212), AllocationEvent::allocate(2, 417),
                               AllocationEvent::allocate(3, 112), AllocationEvent::allocate(4, 426)};
    out_ << "\n--- Part 1: contiguous allocation ---\n"
         << "Partitions 100 500 200 300 600; processes P1=212 P2=417 P3=112 P4=426 (dynamic, split threshold 1).\n";
    TablePrinter::printAllocationComparison(out_, PerformanceAnalyzer::compareAllocation(blocks, workload));
    MemoryAllocator firstFit(blocks, PlacementAlgorithm::FirstFit);
    PerformanceAnalyzer::replayWorkload(firstFit, workload);
    out_ << "\nFirst Fit memory map after the workload:\n";
    TablePrinter::printMemoryMap(out_, firstFit.blocks());

    // Part 2: page replacement on a textbook reference string.
    const std::vector<PageId> refs = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
    out_ << "\n--- Part 2: page replacement ---\n"
         << "Reference string 7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1 with 3 frames.\n\n";
    const std::unique_ptr<PageReplacementAlgorithm> lru = makeReplacementAlgorithm(ReplacementAlgorithm::LRU);
    TablePrinter::printPageTable(out_, PageReplacementSimulator::run(*lru, 3, refs));
    out_ << "\n";
    TablePrinter::printPageComparison(out_, PerformanceAnalyzer::comparePageReplacement(3, refs));

    // Part 3: Belady's anomaly, computed live rather than quoted.
    const std::vector<PageId> belady = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    const auto faultsWith = [&belady](Size frames) {
        const std::unique_ptr<PageReplacementAlgorithm> fifo = makeReplacementAlgorithm(ReplacementAlgorithm::FIFO);
        return PageReplacementSimulator::run(*fifo, frames, belady, SimulationOptions{false}).metrics.faults;
    };
    const Size three = faultsWith(3);
    const Size four = faultsWith(4);
    out_ << "\n--- Part 3: Belady's anomaly ---\n"
         << "Reference string 1 2 3 4 1 2 5 1 2 3 4 5\n"
         << "FIFO, 3 frames: " << three << " faults\n"
         << "FIFO, 4 frames: " << four << " faults\n";
    if (four > three) {
        out_ << "More frames produced MORE faults with FIFO (Belady's anomaly). LRU and Optimal cannot do this.\n";
    }
    out_ << "\nSample simulation finished.\n";
}

}  // namespace memsim
