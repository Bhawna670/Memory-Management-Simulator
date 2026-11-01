#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Types.h"

namespace memsim {

// One page reference and its effect.
struct StepRecord {
    Size position{0};                          // index in the reference string
    PageId page{0};
    bool fault{false};                         // false = hit
    std::optional<PageId> evicted{};           // set only when a resident page was replaced
    // Frame slots AFTER this reference; nullopt = empty. Empty vector if frame
    // recording was disabled for this run (see SimulationOptions).
    std::vector<std::optional<PageId>> frames{};
};

// replacements counts faults that evicted a page; the first `frameCount`
// distinct pages fill empty frames and are faults but not replacements.
// Ratios are 0.0 when there are no references.
struct PageMetrics {
    Size totalReferences{0};
    Size hits{0};
    Size faults{0};
    Size replacements{0};
    double hitRatio{0.0};
    double faultRatio{0.0};
};

struct SimulationResult {
    std::string algorithm{};
    Size frameCount{0};
    std::vector<StepRecord> steps{};
    PageMetrics metrics{};
};

// Template-method base for page-replacement algorithms.
//
// The base owns everything common: the fixed frame slots, O(1) residency lookup
// (page -> slot), fault/hit detection, slot assignment and the per-step record.
// Subclasses supply only the eviction policy through the hooks below.
//
// Frame slots are fixed: an evicted page's slot is reused by the incoming page,
// and empty slots are filled lowest-index first. This matches textbook tables.
class PageReplacementAlgorithm {
public:
    virtual ~PageReplacementAlgorithm() = default;

    virtual std::string name() const = 0;

    // Starts a fresh run, discarding all previous state.
    // `references` is the full string that will be fed to access(); offline
    // policies (Optimal) may inspect it, online policies ignore it.
    // Throws std::invalid_argument if frameCount == 0 or any page is negative.
    void reset(Size frameCount, const std::vector<PageId>& references);

    // Processes the next reference. Throws std::logic_error before reset(),
    // std::invalid_argument for a negative page.
    // The per-step frame snapshot costs O(frameCount); pass recordFrames = false
    // to skip it (StepRecord::frames stays empty) when only counts are needed.
    StepRecord access(PageId page, bool recordFrames = true);

    Size frameCount() const noexcept { return frames_.size(); }

protected:
    // Hooks. The base guarantees: selectVictim is called only when all frames are
    // full and returns a currently resident page; onEvict(victim) follows it, then
    // onInsert(newPage). `position` is the index of the current reference.
    virtual void onReset(const std::vector<PageId>& references) = 0;
    virtual void onHit(PageId page, Size position) = 0;
    virtual void onInsert(PageId page, Size position) = 0;
    virtual void onEvict(PageId page) = 0;
    virtual PageId selectVictim(Size position) const = 0;

    // Read-only view of the frame slots for policies that need slot order
    // (e.g. deterministic tie-breaking). nullopt = empty slot.
    const std::vector<std::optional<PageId>>& frameSlots() const noexcept { return frames_; }

private:
    std::vector<std::optional<PageId>> frames_;
    std::unordered_map<PageId, Size> slotOf_;  // resident page -> frame slot
    Size nextPosition_{0};
    bool ready_{false};
};

}  // namespace memsim
