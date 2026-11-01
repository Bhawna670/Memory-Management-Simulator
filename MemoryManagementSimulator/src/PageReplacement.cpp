#include "PageReplacement.h"

#include <stdexcept>

namespace memsim {

void PageReplacementAlgorithm::reset(Size frameCount, const std::vector<PageId>& references) {
    if (frameCount == 0) {
        throw std::invalid_argument("frame count must be at least 1");
    }
    for (const PageId page : references) {
        if (page < 0) {
            throw std::invalid_argument("page numbers must be non-negative");
        }
    }
    frames_.assign(frameCount, std::nullopt);
    slotOf_.clear();
    nextPosition_ = 0;
    ready_ = true;
    onReset(references);
}

StepRecord PageReplacementAlgorithm::access(PageId page, bool recordFrames) {
    if (!ready_) {
        throw std::logic_error("reset() must be called before access()");
    }
    if (page < 0) {
        throw std::invalid_argument("page numbers must be non-negative");
    }

    StepRecord step;
    step.position = nextPosition_++;
    step.page = page;

    if (slotOf_.find(page) != slotOf_.end()) {
        step.fault = false;
        onHit(page, step.position);
    } else {
        step.fault = true;
        Size slot = 0;
        if (slotOf_.size() < frames_.size()) {
            // Slots fill contiguously from 0 because slots are only ever reused, never freed.
            slot = slotOf_.size();
        } else {
            const PageId victim = selectVictim(step.position);
            slot = slotOf_.at(victim);
            step.evicted = victim;
            onEvict(victim);
            slotOf_.erase(victim);
        }
        frames_[slot] = page;
        slotOf_[page] = slot;
        onInsert(page, step.position);
    }

    if (recordFrames) {
        step.frames = frames_;
    }
    return step;
}

}  // namespace memsim
