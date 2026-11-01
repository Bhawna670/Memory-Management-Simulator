#pragma once

#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "MemoryAllocator.h"
#include "PageReplacementSimulator.h"
#include "PlacementStrategy.h"
#include "ProcessRegistry.h"
#include "Types.h"
#include "Workload.h"

namespace memsim {

// Interactive menu application. Reads lines from `in` and writes to `out`, so the
// same code runs against std::cin/std::cout and against scripted streams in tests.
// All parsing is delegated to InputValidator and all formatting to TablePrinter;
// all algorithm work to the library classes. End of input exits cleanly.
class CliApp {
public:
    CliApp(std::istream& in, std::ostream& out);

    // Runs until the user exits or input ends. Returns the process exit code (0).
    int run();

private:
    // ---- I/O helpers ----
    std::optional<std::string> readLine(const std::string& prompt);
    std::optional<Size> askChoice(Size minChoice, Size maxChoice);
    // Re-prompts until `parse` accepts the text. Typing "cancel" or reaching end of
    // input returns nullopt.
    template <typename T, typename Parser>
    std::optional<T> ask(const std::string& prompt, Parser parse);

    // ---- menus ----
    void printMainMenu();
    void memoryMenu();
    void pageMenu();
    void performanceMenu();
    void fragmentationAnalysis();
    void runSampleSimulation();

    // ---- memory allocation actions ----
    bool requireAllocator();
    void initializeMemory();
    void addProcess();
    void selectPlacementAlgorithm();
    void allocateProcess();
    void deallocateProcess();
    void displayMemoryMap();
    void displayAllocationStats();

    // ---- page replacement actions ----
    void configureFrameCount();
    void enterReferenceString();
    void selectReplacementAlgorithm();
    void runPageSimulation();
    void displayPageTable();
    void comparePageAlgorithms();

    // ---- performance actions ----
    void compareAllocationAlgorithms();

    std::istream& in_;
    std::ostream& out_;
    bool eof_{false};

    // memory allocation session
    std::optional<MemoryAllocator> allocator_;
    std::vector<Size> blockSizes_;
    AllocatorConfig config_{};
    PlacementAlgorithm placement_{PlacementAlgorithm::FirstFit};
    ProcessRegistry registry_;
    Workload history_;  // every allocate/deallocate sent to the allocator, for replay

    // page replacement session
    Size frameCount_{0};
    bool frameCountSet_{false};
    std::vector<PageId> references_;
    bool referencesSet_{false};
    ReplacementAlgorithm replacement_{ReplacementAlgorithm::FIFO};
    std::optional<SimulationResult> lastResult_;
};

}  // namespace memsim
