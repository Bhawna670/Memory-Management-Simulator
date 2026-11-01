#include "TablePrinter.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace memsim {

namespace {

std::string num(Size value) { return std::to_string(value); }

// Frame lists wider than this are abbreviated in the page table so that a run with
// hundreds of frames stays readable. The simulation data itself is never truncated.
constexpr std::size_t kMaxShownFrames = 16;

std::string framesText(const std::vector<std::optional<PageId>>& frames) {
    const std::size_t shown = std::min(frames.size(), kMaxShownFrames);
    std::string text = "[";
    for (std::size_t i = 0; i < shown; ++i) {
        text += (i == 0 ? "" : " ");
        text += frames[i].has_value() ? std::to_string(*frames[i]) : "-";
    }
    if (frames.size() > shown) {
        text += " ... +" + std::to_string(frames.size() - shown) + " more";
    }
    return text + "]";
}

std::string joinSizes(const std::vector<Size>& sizes) {
    std::string text;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        text += (i == 0 ? "" : " ") + std::to_string(sizes[i]);
    }
    return text;
}

}  // namespace

std::string TablePrinter::formatPercent(double ratio) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << ratio * 100.0 << "%";
    return out.str();
}

std::string TablePrinter::formatRatio(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

void TablePrinter::printTable(std::ostream& os, const std::vector<std::string>& headers,
                              const std::vector<std::vector<std::string>>& rows,
                              const std::vector<bool>& rightAlign) {
    std::vector<std::size_t> width(headers.size());
    for (std::size_t c = 0; c < headers.size(); ++c) {
        width[c] = headers[c].size();
        for (const auto& row : rows) {
            width[c] = std::max(width[c], row[c].size());
        }
    }
    const auto border = [&]() {
        os << '+';
        for (const std::size_t w : width) {
            os << std::string(w + 2, '-') << '+';
        }
        os << '\n';
    };
    const auto line = [&](const std::vector<std::string>& cells, bool isHeader) {
        os << '|';
        for (std::size_t c = 0; c < cells.size(); ++c) {
            const std::string pad(width[c] - cells[c].size(), ' ');
            const bool right = !isHeader && c < rightAlign.size() && rightAlign[c];
            os << ' ' << (right ? pad + cells[c] : cells[c] + pad) << " |";
        }
        os << '\n';
    };
    border();
    line(headers, true);
    border();
    for (const auto& row : rows) {
        line(row, false);
    }
    border();
}

void TablePrinter::printMemoryMap(std::ostream& os, const std::vector<MemoryBlock>& blocks) {
    std::vector<std::vector<std::string>> rows;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const MemoryBlock& b = blocks[i];
        const bool free = b.isFree();
        rows.push_back({num(i), num(b.start), num(b.end() - 1), num(b.size), toString(b.state),
                        free ? "-" : "P" + std::to_string(*b.owner), free ? "-" : num(b.requestedSize),
                        free ? "-" : num(b.internalFragmentation())});
    }
    printTable(os, {"#", "Start", "End", "Size", "State", "Process", "Requested", "Internal frag"}, rows,
               {true, true, true, true, false, false, true, true});
}

void TablePrinter::printProcessTable(std::ostream& os, const ProcessRegistry& registry) {
    std::vector<std::vector<std::string>> rows;
    for (const Process& p : registry.processes()) {
        rows.push_back({"P" + std::to_string(p.id), num(p.size), toString(p.state)});
    }
    printTable(os, {"Process", "Size", "State"}, rows, {false, true, false});
}

void TablePrinter::printFragmentation(std::ostream& os, const FragmentationReport& r) {
    printTable(os, {"Metric", "Value"},
               {{"Total memory", num(r.totalMemory)},
                {"Allocated memory (block sizes)", num(r.allocatedMemory)},
                {"Requested by processes", num(r.requestedMemory)},
                {"Free memory", num(r.freeMemory)},
                {"Internal fragmentation", num(r.internalFragmentation)},
                {"Number of free blocks", num(r.freeBlockCount)},
                {"Largest free block", num(r.largestFreeBlock)},
                {"External fragmentation indicator", formatRatio(r.externalFragmentation)}},
               {false, true});
    os << "Internal fragmentation = allocated block size - requested size, summed over allocated blocks.\n"
       << "  (Unused space inside a free block is NOT internal fragmentation.)\n"
       << "External fragmentation indicator = 1 - largest free block / total free memory\n"
       << "  (0 = free space is one piece or there is none; closer to 1 = free space is scattered.)\n";
}

void TablePrinter::printAllocationStats(std::ostream& os, const MemoryAllocator& allocator) {
    const AllocationMetrics m = PerformanceAnalyzer::analyzeAllocation(allocator);
    const AllocatorConfig& cfg = allocator.config();
    os << "Algorithm: " << toString(allocator.algorithm()) << "   Mode: ";
    if (!cfg.mergeOnFree && cfg.splitThreshold == AllocatorConfig::fixedPartitions().splitThreshold) {
        os << "fixed partitions (no split, no merge)\n";
    } else {
        os << "dynamic partitions (split threshold " << cfg.splitThreshold
           << (cfg.mergeOnFree ? ", merge on free)\n" : ", no merge)\n");
    }
    const FragmentationReport& f = m.fragmentation;
    printTable(os, {"Metric", "Value"},
               {{"Total processes (allocation attempts)", num(m.totalProcesses)},
                {"Successfully allocated", num(m.successfulAllocations)},
                {"Failed allocations", num(m.failedAllocations)},
                {"  of which: enough total free memory", num(m.failedDespiteEnoughFree)},
                {"Allocation success rate", formatPercent(m.successRate)},
                {"Total memory", num(f.totalMemory)},
                {"Allocated memory", num(f.allocatedMemory)},
                {"Free memory", num(f.freeMemory)},
                {"Memory utilization (allocated)", formatPercent(m.allocatedUtilization)},
                {"Memory utilization (requested)", formatPercent(m.effectiveUtilization)},
                {"Internal fragmentation", num(f.internalFragmentation)},
                {"Free blocks", num(f.freeBlockCount)},
                {"Largest free block", num(f.largestFreeBlock)},
                {"External fragmentation indicator", formatRatio(f.externalFragmentation)}},
               {false, true});
}

void TablePrinter::printPageTable(std::ostream& os, const SimulationResult& result) {
    std::vector<std::vector<std::string>> rows;
    for (const StepRecord& s : result.steps) {
        rows.push_back({num(s.position + 1), std::to_string(s.page),
                        s.frames.empty() ? "(not recorded)" : framesText(s.frames),
                        s.fault ? "FAULT" : "HIT", s.evicted.has_value() ? std::to_string(*s.evicted) : "-"});
    }
    os << result.algorithm << " with " << result.frameCount << " frame(s)\n";
    printTable(os, {"Step", "Page", "Frames", "Result", "Replaced"}, rows, {true, true, false, false, true});
    if (result.frameCount > kMaxShownFrames) {
        os << "Only the first " << kMaxShownFrames << " of " << result.frameCount
           << " frames are shown in each row.\n";
    }
}

void TablePrinter::printPageMetrics(std::ostream& os, const SimulationResult& result) {
    const PageMetrics& m = result.metrics;
    printTable(os, {"Metric", "Value"},
               {{"Algorithm", result.algorithm},
                {"Frames", num(result.frameCount)},
                {"Total page references", num(m.totalReferences)},
                {"Page hits", num(m.hits)},
                {"Page faults", num(m.faults)},
                {"Replacements", num(m.replacements)},
                {"Hit ratio", formatPercent(m.hitRatio)},
                {"Fault ratio", formatPercent(m.faultRatio)}},
               {false, true});
}

void TablePrinter::printPageComparison(std::ostream& os, const PageComparison& comparison) {
    const SimulationResult* optimal = comparison.find("Optimal");
    std::vector<std::vector<std::string>> rows;
    for (const SimulationResult& r : comparison.results) {
        rows.push_back({r.algorithm, num(r.metrics.faults), num(r.metrics.hits), num(r.metrics.replacements),
                        formatPercent(r.metrics.hitRatio), formatPercent(r.metrics.faultRatio),
                        optimal != nullptr ? num(r.metrics.faults - optimal->metrics.faults) : "-"});
    }
    os << comparison.totalReferences << " reference(s), " << comparison.frameCount << " frame(s)\n";
    printTable(os, {"Algorithm", "Faults", "Hits", "Replacements", "Hit ratio", "Fault ratio", "Faults above Optimal"},
               rows, {false, true, true, true, true, true, true});
    os << "These results apply to this reference string and frame count only.\n"
       << "Optimal is an offline benchmark (it needs the whole future reference string)\n"
       << "and cannot be implemented in a real operating system.\n";
}

void TablePrinter::printAllocationComparison(std::ostream& os, const AllocationComparison& comparison) {
    os << "Memory blocks: " << joinSizes(comparison.blockSizes) << "   Events replayed: " << comparison.eventCount
       << "   Split threshold: " << comparison.config.splitThreshold
       << (comparison.config.mergeOnFree ? "   Merge on free\n" : "   No merge\n");

    std::vector<std::string> headers = {"Metric"};
    for (const AllocationRunResult& run : comparison.runs) {
        headers.push_back(toString(run.algorithm));
    }
    const auto row = [&](const std::string& label, auto&& extract) {
        std::vector<std::string> cells = {label};
        for (const AllocationRunResult& run : comparison.runs) {
            cells.push_back(extract(run));
        }
        return cells;
    };
    std::vector<std::vector<std::string>> rows;
    rows.push_back(row("Total processes (attempts)", [](const AllocationRunResult& r) { return num(r.metrics.totalProcesses); }));
    rows.push_back(row("Successfully allocated", [](const AllocationRunResult& r) { return num(r.metrics.successfulAllocations); }));
    rows.push_back(row("Failed allocations", [](const AllocationRunResult& r) { return num(r.metrics.failedAllocations); }));
    rows.push_back(row("  failed with enough total free", [](const AllocationRunResult& r) { return num(r.metrics.failedDespiteEnoughFree); }));
    rows.push_back(row("Allocation success rate", [](const AllocationRunResult& r) { return formatPercent(r.metrics.successRate); }));
    rows.push_back(row("Utilization (allocated)", [](const AllocationRunResult& r) { return formatPercent(r.metrics.allocatedUtilization); }));
    rows.push_back(row("Utilization (requested)", [](const AllocationRunResult& r) { return formatPercent(r.metrics.effectiveUtilization); }));
    rows.push_back(row("Peak allocated memory", [](const AllocationRunResult& r) { return num(r.peakAllocatedMemory); }));
    rows.push_back(row("Internal fragmentation", [](const AllocationRunResult& r) { return num(r.metrics.fragmentation.internalFragmentation); }));
    rows.push_back(row("Free blocks", [](const AllocationRunResult& r) { return num(r.metrics.fragmentation.freeBlockCount); }));
    rows.push_back(row("Largest free block", [](const AllocationRunResult& r) { return num(r.metrics.fragmentation.largestFreeBlock); }));
    rows.push_back(row("External fragmentation ind.", [](const AllocationRunResult& r) { return formatRatio(r.metrics.fragmentation.externalFragmentation); }));
    rows.push_back(row("Rejected requests", [](const AllocationRunResult& r) { return num(r.rejectedRequests); }));
    rows.push_back(row("Deallocation misses", [](const AllocationRunResult& r) { return num(r.deallocationMisses); }));

    std::vector<bool> rightAlign(headers.size(), true);
    rightAlign[0] = false;
    printTable(os, headers, rows, rightAlign);
    os << "Every algorithm replayed the same events on identically configured memory.\n"
       << "Results apply to this workload only; no algorithm is best in general.\n";
}

}  // namespace memsim
