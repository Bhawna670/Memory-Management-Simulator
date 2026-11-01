#include "PlacementStrategy.h"

#include <stdexcept>

#include "BestFit.h"
#include "FirstFit.h"
#include "WorstFit.h"

namespace memsim {

std::string toString(PlacementAlgorithm algorithm) {
    switch (algorithm) {
        case PlacementAlgorithm::FirstFit: return "First Fit";
        case PlacementAlgorithm::BestFit:  return "Best Fit";
        case PlacementAlgorithm::WorstFit: return "Worst Fit";
    }
    return "Unknown";
}

std::unique_ptr<PlacementStrategy> makePlacementStrategy(PlacementAlgorithm algorithm) {
    switch (algorithm) {
        case PlacementAlgorithm::FirstFit: return std::make_unique<FirstFit>();
        case PlacementAlgorithm::BestFit:  return std::make_unique<BestFit>();
        case PlacementAlgorithm::WorstFit: return std::make_unique<WorstFit>();
    }
    throw std::invalid_argument("unknown placement algorithm");
}

}  // namespace memsim
