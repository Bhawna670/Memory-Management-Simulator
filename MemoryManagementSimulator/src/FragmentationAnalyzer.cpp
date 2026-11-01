#include "FragmentationAnalyzer.h"

#include <algorithm>

namespace memsim {

FragmentationReport FragmentationAnalyzer::analyze(const std::vector<MemoryBlock>& blocks) {
    FragmentationReport report;
    for (const MemoryBlock& block : blocks) {
        report.totalMemory += block.size;
        if (block.isFree()) {
            report.freeMemory += block.size;
            ++report.freeBlockCount;
            report.largestFreeBlock = std::max(report.largestFreeBlock, block.size);
        } else {
            report.allocatedMemory += block.size;
            report.requestedMemory += block.requestedSize;
            report.internalFragmentation += block.internalFragmentation();
        }
    }
    report.externalFragmentation =
        externalFragmentationIndicator(report.largestFreeBlock, report.freeMemory);
    return report;
}

double FragmentationAnalyzer::externalFragmentationIndicator(Size largestFreeBlock,
                                                             Size totalFreeMemory) noexcept {
    if (totalFreeMemory == 0) {
        return 0.0;
    }
    return 1.0 - static_cast<double>(largestFreeBlock) / static_cast<double>(totalFreeMemory);
}

}  // namespace memsim
