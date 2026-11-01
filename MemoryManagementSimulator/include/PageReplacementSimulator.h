#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PageReplacement.h"
#include "Types.h"

namespace memsim {

enum class ReplacementAlgorithm { FIFO, LRU, Optimal };

std::string toString(ReplacementAlgorithm algorithm);

// FIFO, LRU, Optimal, in that order (the order used by comparisons).
std::vector<ReplacementAlgorithm> allReplacementAlgorithms();

// Throws std::invalid_argument for an out-of-range enum value.
std::unique_ptr<PageReplacementAlgorithm> makeReplacementAlgorithm(ReplacementAlgorithm algorithm);

struct SimulationOptions {
    // false skips the O(frames) per-step frame snapshot; fault/hit/eviction data
    // and metrics are unaffected, StepRecord::frames is left empty.
    bool recordFrames{true};
};

// Results for every algorithm on one identical input.
struct PageComparison {
    Size frameCount{0};
    Size totalReferences{0};
    std::vector<SimulationResult> results{};  // FIFO, LRU, Optimal

    // nullptr if no result has this algorithm name.
    const SimulationResult* find(const std::string& algorithmName) const;
};

class PageReplacementSimulator {
public:
    // Resets `algorithm`, feeds it every reference in order, and returns the full trace.
    // Propagates std::invalid_argument from reset() for a zero frame count or negative page.
    static SimulationResult run(PageReplacementAlgorithm& algorithm, Size frameCount,
                                const std::vector<PageId>& references,
                                SimulationOptions options = SimulationOptions{});

    // Runs FIFO, LRU and Optimal, each on a fresh instance, with the same frame
    // count and the same reference string.
    static PageComparison compareAll(Size frameCount, const std::vector<PageId>& references,
                                     SimulationOptions options = SimulationOptions{});

    // Derives metrics from a trace. Safe for an empty trace (all zeros).
    static PageMetrics computeMetrics(const std::vector<StepRecord>& steps);
};

}  // namespace memsim
