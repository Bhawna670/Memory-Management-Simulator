#include "PageReplacementSimulator.h"

#include <stdexcept>

#include "FIFO.h"
#include "LRU.h"
#include "Optimal.h"

namespace memsim {

std::string toString(ReplacementAlgorithm algorithm) {
    switch (algorithm) {
        case ReplacementAlgorithm::FIFO:    return "FIFO";
        case ReplacementAlgorithm::LRU:     return "LRU";
        case ReplacementAlgorithm::Optimal: return "Optimal";
    }
    return "Unknown";
}

std::vector<ReplacementAlgorithm> allReplacementAlgorithms() {
    return {ReplacementAlgorithm::FIFO, ReplacementAlgorithm::LRU, ReplacementAlgorithm::Optimal};
}

std::unique_ptr<PageReplacementAlgorithm> makeReplacementAlgorithm(ReplacementAlgorithm algorithm) {
    switch (algorithm) {
        case ReplacementAlgorithm::FIFO:    return std::make_unique<FIFO>();
        case ReplacementAlgorithm::LRU:     return std::make_unique<LRU>();
        case ReplacementAlgorithm::Optimal: return std::make_unique<Optimal>();
    }
    throw std::invalid_argument("unknown replacement algorithm");
}

const SimulationResult* PageComparison::find(const std::string& algorithmName) const {
    for (const SimulationResult& result : results) {
        if (result.algorithm == algorithmName) {
            return &result;
        }
    }
    return nullptr;
}

SimulationResult PageReplacementSimulator::run(PageReplacementAlgorithm& algorithm,
                                               Size frameCount,
                                               const std::vector<PageId>& references,
                                               SimulationOptions options) {
    algorithm.reset(frameCount, references);

    SimulationResult result;
    result.algorithm = algorithm.name();
    result.frameCount = frameCount;
    result.steps.reserve(references.size());
    for (const PageId page : references) {
        result.steps.push_back(algorithm.access(page, options.recordFrames));
    }
    result.metrics = computeMetrics(result.steps);
    return result;
}

PageComparison PageReplacementSimulator::compareAll(Size frameCount,
                                                    const std::vector<PageId>& references,
                                                    SimulationOptions options) {
    PageComparison comparison;
    comparison.frameCount = frameCount;
    comparison.totalReferences = references.size();
    for (const ReplacementAlgorithm kind : allReplacementAlgorithms()) {
        const std::unique_ptr<PageReplacementAlgorithm> algorithm = makeReplacementAlgorithm(kind);
        comparison.results.push_back(run(*algorithm, frameCount, references, options));
    }
    return comparison;
}

PageMetrics PageReplacementSimulator::computeMetrics(const std::vector<StepRecord>& steps) {
    PageMetrics m;
    m.totalReferences = steps.size();
    for (const StepRecord& step : steps) {
        if (step.fault) {
            ++m.faults;
            if (step.evicted.has_value()) {
                ++m.replacements;
            }
        } else {
            ++m.hits;
        }
    }
    if (m.totalReferences > 0) {
        m.hitRatio = static_cast<double>(m.hits) / static_cast<double>(m.totalReferences);
        m.faultRatio = static_cast<double>(m.faults) / static_cast<double>(m.totalReferences);
    }
    return m;
}

}  // namespace memsim
