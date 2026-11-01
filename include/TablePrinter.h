#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "FragmentationAnalyzer.h"
#include "MemoryAllocator.h"
#include "MemoryBlock.h"
#include "PageReplacementSimulator.h"
#include "PerformanceAnalyzer.h"
#include "ProcessRegistry.h"

namespace memsim {

// Formats results as plain-ASCII tables. Writes to any std::ostream so output is testable.
class TablePrinter {
public:
    static void printMemoryMap(std::ostream& os, const std::vector<MemoryBlock>& blocks);
    static void printProcessTable(std::ostream& os, const ProcessRegistry& registry);
    static void printFragmentation(std::ostream& os, const FragmentationReport& report);
    static void printAllocationStats(std::ostream& os, const MemoryAllocator& allocator);
    static void printPageTable(std::ostream& os, const SimulationResult& result);
    static void printPageMetrics(std::ostream& os, const SimulationResult& result);
    static void printPageComparison(std::ostream& os, const PageComparison& comparison);
    static void printAllocationComparison(std::ostream& os, const AllocationComparison& comparison);

    static std::string formatPercent(double ratio);  // 0.25 -> "25.00%"
    static std::string formatRatio(double value);    // 0.5  -> "0.500"

private:
    static void printTable(std::ostream& os, const std::vector<std::string>& headers,
                           const std::vector<std::vector<std::string>>& rows,
                           const std::vector<bool>& rightAlign);
};

}  // namespace memsim
