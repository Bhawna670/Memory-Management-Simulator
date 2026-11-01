// Page-replacement tests: shared base, FIFO, LRU and Optimal.
// Expected traces are hand-derived (see comments), not copied from program output.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "FIFO.h"
#include "LRU.h"
#include "Optimal.h"
#include "PageReplacementSimulator.h"
#include "TestHarness.h"

using namespace memsim;

namespace {

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

constexpr int E = -1;  // empty frame slot in expected tables

// Frame contents as ints (E for an empty slot) for easy comparison.
std::vector<int> snapshot(const StepRecord& step) {
    std::vector<int> out;
    for (const auto& slot : step.frames) {
        out.push_back(slot.has_value() ? *slot : E);
    }
    return out;
}

std::string show(const std::vector<int>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        s += (i ? "," : "") + (v[i] == E ? std::string("-") : std::to_string(v[i]));
    }
    return s + "]";
}

SimulationResult runFifo(Size frames, const std::vector<PageId>& refs) {
    FIFO fifo;
    return PageReplacementSimulator::run(fifo, frames, refs);
}

// Silberschatz textbook reference string.
const std::vector<PageId> kTextbookRefs = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};

// ---------- Edge cases ----------

void test_empty_reference_string() {
    const auto r = runFifo(3, {});
    CHECK_EQ(r.steps.size(), 0);
    CHECK_EQ(r.metrics.totalReferences, 0);
    CHECK_EQ(r.metrics.hits, 0);
    CHECK_EQ(r.metrics.faults, 0);
    CHECK_EQ(r.metrics.replacements, 0);
    CHECK(near(r.metrics.hitRatio, 0.0));
    CHECK(near(r.metrics.faultRatio, 0.0));
    CHECK(!std::isnan(r.metrics.hitRatio));
}

void test_single_frame() {
    // 1 1 2 2 1 with one frame: F H F(evict 1) H F(evict 2)
    const auto r = runFifo(1, {1, 1, 2, 2, 1});
    CHECK_EQ(r.metrics.faults, 3);
    CHECK_EQ(r.metrics.hits, 2);
    CHECK_EQ(r.metrics.replacements, 2);
    CHECK(r.steps[0].fault && !r.steps[1].fault && r.steps[2].fault && !r.steps[3].fault && r.steps[4].fault);
    CHECK(!r.steps[0].evicted.has_value());
    CHECK(r.steps[2].evicted == std::optional<PageId>(1));
    CHECK(r.steps[4].evicted == std::optional<PageId>(2));
    CHECK(snapshot(r.steps[4]) == std::vector<int>({1}));
}

void test_repeated_page_faults_once() {
    const auto r = runFifo(3, {5, 5, 5, 5});
    CHECK_EQ(r.metrics.faults, 1);
    CHECK_EQ(r.metrics.hits, 3);
    CHECK_EQ(r.metrics.replacements, 0);
    CHECK(snapshot(r.steps[3]) == std::vector<int>({5, E, E}));
    CHECK(near(r.metrics.hitRatio, 0.75));
    CHECK(near(r.metrics.faultRatio, 0.25));
}

void test_all_unique_references() {
    // 1..6, 3 frames: every reference faults; evictions 1, 2, 3 in arrival order.
    const auto r = runFifo(3, {1, 2, 3, 4, 5, 6});
    CHECK_EQ(r.metrics.faults, 6);
    CHECK_EQ(r.metrics.hits, 0);
    CHECK_EQ(r.metrics.replacements, 3);  // first three faults only filled empty frames
    CHECK(r.steps[3].evicted == std::optional<PageId>(1));
    CHECK(r.steps[4].evicted == std::optional<PageId>(2));
    CHECK(r.steps[5].evicted == std::optional<PageId>(3));
    CHECK(snapshot(r.steps[5]) == std::vector<int>({4, 5, 6}));
}

void test_more_frames_than_distinct_pages() {
    const auto r = runFifo(5, {1, 2, 1, 2});
    CHECK_EQ(r.metrics.faults, 2);
    CHECK_EQ(r.metrics.replacements, 0);
    CHECK(snapshot(r.steps[3]) == std::vector<int>({1, 2, E, E, E}));
}

// ---------- FIFO behavior ----------

void test_hit_does_not_protect_oldest_page() {
    // 1 2 3 1 4 with 3 frames: the hit on 1 does not refresh it, so 4 still evicts 1.
    const auto r = runFifo(3, {1, 2, 3, 1, 4});
    CHECK(!r.steps[3].fault);
    CHECK(r.steps[4].fault);
    CHECK(r.steps[4].evicted == std::optional<PageId>(1));
    CHECK(snapshot(r.steps[4]) == std::vector<int>({4, 2, 3}));
}

void test_textbook_fifo_step_by_step() {
    // Hand trace, 3 frames. Slot is reused by the replacing page.
    const std::vector<std::vector<int>> frames = {
        {7, E, E}, {7, 0, E}, {7, 0, 1}, {2, 0, 1}, {2, 0, 1}, {2, 3, 1}, {2, 3, 0},
        {4, 3, 0}, {4, 2, 0}, {4, 2, 3}, {0, 2, 3}, {0, 2, 3}, {0, 2, 3}, {0, 1, 3},
        {0, 1, 2}, {0, 1, 2}, {0, 1, 2}, {7, 1, 2}, {7, 0, 2}, {7, 0, 1}};
    const std::vector<bool> fault = {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1};
    // evicted page per step; -1 means none
    const std::vector<int> evicted = {E, E, E, 7, E, 0, 1, 2, 3, 0, 4, E, E, 2, 3, E, E, 0, 1, 2};

    const auto r = runFifo(3, kTextbookRefs);
    CHECK_EQ(r.steps.size(), 20);
    for (std::size_t i = 0; i < r.steps.size(); ++i) {
        const StepRecord& s = r.steps[i];
        CHECK_EQ(s.position, i);
        CHECK_EQ(s.page, kTextbookRefs[i]);
        CHECK_EQ(s.fault, fault[i]);
        const int ev = s.evicted.has_value() ? *s.evicted : E;
        CHECK_EQ(ev, evicted[i]);
        const std::vector<int> got = snapshot(s);
        if (got != frames[i]) {
            CHECK(got == frames[i]);
            std::cerr << "  step " << i << ": got " << show(got) << " expected " << show(frames[i]) << "\n";
        }
    }
    CHECK_EQ(r.metrics.faults, 15);
    CHECK_EQ(r.metrics.hits, 5);
    CHECK_EQ(r.metrics.replacements, 12);  // 15 faults - 3 that filled empty frames
    CHECK(near(r.metrics.hitRatio, 0.25));
    CHECK(near(r.metrics.faultRatio, 0.75));
}

void test_beladys_anomaly() {
    // 1 2 3 4 1 2 5 1 2 3 4 5: FIFO faults 9 with 3 frames but 10 with 4 frames.
    const std::vector<PageId> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    CHECK_EQ(runFifo(3, refs).metrics.faults, 9);
    CHECK_EQ(runFifo(4, refs).metrics.faults, 10);
}

void test_metrics_are_internally_consistent() {
    const auto r = runFifo(3, kTextbookRefs);
    CHECK_EQ(r.metrics.hits + r.metrics.faults, r.metrics.totalReferences);
    CHECK_EQ(r.metrics.totalReferences, kTextbookRefs.size());
    CHECK(near(r.metrics.hitRatio + r.metrics.faultRatio, 1.0));
    CHECK(r.metrics.replacements <= r.metrics.faults);
    CHECK_EQ(r.algorithm, std::string("FIFO"));
    CHECK_EQ(r.frameCount, 3);
}

// ---------- Determinism, reuse, validation ----------

void test_run_is_deterministic_and_reusable() {
    FIFO fifo;
    const auto first = PageReplacementSimulator::run(fifo, 3, kTextbookRefs);
    const auto second = PageReplacementSimulator::run(fifo, 3, kTextbookRefs);  // same object, state must reset
    CHECK_EQ(first.metrics.faults, second.metrics.faults);
    for (std::size_t i = 0; i < first.steps.size(); ++i) {
        CHECK(snapshot(first.steps[i]) == snapshot(second.steps[i]));
    }
    // A different run on the same object must not inherit old state.
    const auto other = PageReplacementSimulator::run(fifo, 1, {1, 2});
    CHECK_EQ(other.metrics.faults, 2);
    CHECK(snapshot(other.steps[1]) == std::vector<int>({2}));
}

void test_zero_frames_rejected() {
    FIFO fifo;
    CHECK_THROWS(PageReplacementSimulator::run(fifo, 0, {1, 2}), std::invalid_argument);
}

void test_negative_page_rejected() {
    FIFO fifo;
    CHECK_THROWS(PageReplacementSimulator::run(fifo, 2, {1, -2, 3}), std::invalid_argument);
}

void test_access_before_reset_rejected() {
    FIFO fifo;
    CHECK_THROWS(fifo.access(1), std::logic_error);
}

void test_page_zero_is_valid() {
    const auto r = runFifo(2, {0, 0, 1});
    CHECK_EQ(r.metrics.faults, 2);
    CHECK(snapshot(r.steps[2]) == std::vector<int>({0, 1}));
}

void test_compute_metrics_on_empty_trace() {
    const PageMetrics m = PageReplacementSimulator::computeMetrics({});
    CHECK_EQ(m.totalReferences, 0);
    CHECK(near(m.hitRatio, 0.0));
}

// ---------- LRU ----------

SimulationResult runLru(Size frames, const std::vector<PageId>& refs) {
    LRU lru;
    return PageReplacementSimulator::run(lru, frames, refs);
}

void test_lru_empty_reference_string() {
    const auto r = runLru(3, {});
    CHECK_EQ(r.steps.size(), 0);
    CHECK_EQ(r.metrics.faults, 0);
    CHECK(near(r.metrics.hitRatio, 0.0));
    CHECK(near(r.metrics.faultRatio, 0.0));
}

void test_lru_single_frame() {
    const auto r = runLru(1, {1, 1, 2, 2, 1});  // F H F H F
    CHECK_EQ(r.metrics.faults, 3);
    CHECK_EQ(r.metrics.hits, 2);
    CHECK_EQ(r.metrics.replacements, 2);
    CHECK(r.steps[2].evicted == std::optional<PageId>(1));
    CHECK(r.steps[4].evicted == std::optional<PageId>(2));
}

void test_lru_repeated_page() {
    const auto r = runLru(3, {5, 5, 5, 5});
    CHECK_EQ(r.metrics.faults, 1);
    CHECK_EQ(r.metrics.hits, 3);
    CHECK(snapshot(r.steps[3]) == std::vector<int>({5, E, E}));
}

void test_lru_all_unique_references() {
    // Pages are never reused, so LRU order equals arrival order: evicts 1, 2, 3.
    const auto r = runLru(3, {1, 2, 3, 4, 5, 6});
    CHECK_EQ(r.metrics.faults, 6);
    CHECK_EQ(r.metrics.replacements, 3);
    CHECK(r.steps[3].evicted == std::optional<PageId>(1));
    CHECK(r.steps[4].evicted == std::optional<PageId>(2));
    CHECK(r.steps[5].evicted == std::optional<PageId>(3));
    CHECK(snapshot(r.steps[5]) == std::vector<int>({4, 5, 6}));
}

void test_lru_hit_refreshes_recency() {
    // 1 2 3 1 4, 3 frames: the hit on 1 makes 2 the least recently used, so 4 evicts 2.
    // (FIFO evicts 1 on the same input.)
    const auto r = runLru(3, {1, 2, 3, 1, 4});
    CHECK(!r.steps[3].fault);
    CHECK(r.steps[4].evicted == std::optional<PageId>(2));
    CHECK(snapshot(r.steps[4]) == std::vector<int>({1, 4, 3}));
    CHECK(runFifo(3, {1, 2, 3, 1, 4}).steps[4].evicted == std::optional<PageId>(1));
}

void test_lru_replacement_order_with_multiple_hits() {
    // 1 2 3 2 1 4 5, 3 frames. Recency (MRU first) after each step:
    //   3,2,1 -> 2,3,1 -> 1,2,3 ; 4 evicts 3 -> 4,1,2 ; 5 evicts 2.
    const auto r = runLru(3, {1, 2, 3, 2, 1, 4, 5});
    CHECK_EQ(r.metrics.faults, 5);
    CHECK(r.steps[5].evicted == std::optional<PageId>(3));
    CHECK(snapshot(r.steps[5]) == std::vector<int>({1, 2, 4}));
    CHECK(r.steps[6].evicted == std::optional<PageId>(2));
    CHECK(snapshot(r.steps[6]) == std::vector<int>({1, 5, 4}));
}

void test_textbook_lru_step_by_step() {
    // Hand trace, 3 frames, recency list MRU-first noted in the derivation.
    const std::vector<std::vector<int>> frames = {
        {7, E, E}, {7, 0, E}, {7, 0, 1}, {2, 0, 1}, {2, 0, 1}, {2, 0, 3}, {2, 0, 3},
        {4, 0, 3}, {4, 0, 2}, {4, 3, 2}, {0, 3, 2}, {0, 3, 2}, {0, 3, 2}, {1, 3, 2},
        {1, 3, 2}, {1, 0, 2}, {1, 0, 2}, {1, 0, 7}, {1, 0, 7}, {1, 0, 7}};
    const std::vector<bool> fault = {1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0};
    const std::vector<int> evicted = {E, E, E, 7, E, 1, E, 2, 3, 0, 4, E, E, 0, E, 3, E, 2, E, E};

    const auto r = runLru(3, kTextbookRefs);
    CHECK_EQ(r.steps.size(), 20);
    for (std::size_t i = 0; i < r.steps.size(); ++i) {
        const StepRecord& s = r.steps[i];
        CHECK_EQ(s.fault, fault[i]);
        const int ev = s.evicted.has_value() ? *s.evicted : E;
        CHECK_EQ(ev, evicted[i]);
        const std::vector<int> got = snapshot(s);
        if (got != frames[i]) {
            CHECK(got == frames[i]);
            std::cerr << "  step " << i << ": got " << show(got) << " expected " << show(frames[i]) << "\n";
        }
    }
    CHECK_EQ(r.metrics.faults, 12);
    CHECK_EQ(r.metrics.hits, 8);
    CHECK_EQ(r.metrics.replacements, 9);  // 12 faults - 3 that filled empty frames
    CHECK(near(r.metrics.hitRatio, 0.4));
    CHECK(near(r.metrics.faultRatio, 0.6));
}

void test_lru_beats_fifo_on_textbook_string() {
    CHECK_EQ(runLru(3, kTextbookRefs).metrics.faults, 12);
    CHECK_EQ(runFifo(3, kTextbookRefs).metrics.faults, 15);
}

void test_lru_has_no_beladys_anomaly_on_classic_string() {
    // 1 2 3 4 1 2 5 1 2 3 4 5: LRU faults 10 with 3 frames and 8 with 4 (FIFO: 9 then 10).
    const std::vector<PageId> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    CHECK_EQ(runLru(3, refs).metrics.faults, 10);
    CHECK_EQ(runLru(4, refs).metrics.faults, 8);
}

std::vector<PageId> pseudoRandomRefs(std::uint32_t seed, Size length, PageId pageRange) {
    std::uint32_t state = seed;
    std::vector<PageId> refs;
    for (Size i = 0; i < length; ++i) {
        state = state * 1664525u + 1013904223u;
        refs.push_back(static_cast<PageId>((state >> 16) % static_cast<std::uint32_t>(pageRange)));
    }
    return refs;
}

void test_lru_stack_property_more_frames_never_more_faults() {
    // LRU is a stack algorithm: faults are non-increasing in the frame count.
    for (const auto& refs : {kTextbookRefs, pseudoRandomRefs(7u, 600, 12), pseudoRandomRefs(99u, 600, 30)}) {
        Size previous = runLru(1, refs).metrics.faults;
        for (Size frames = 2; frames <= 10; ++frames) {
            const Size current = runLru(frames, refs).metrics.faults;
            CHECK(current <= previous);
            previous = current;
        }
    }
}

// Independent O(n*f) LRU: linear scans and per-slot timestamps, no list or hash map.
void expectMatchesNaiveLru(Size frames, const std::vector<PageId>& refs) {
    const auto r = runLru(frames, refs);
    CHECK_EQ(r.steps.size(), refs.size());

    std::vector<std::pair<PageId, Size>> slots;  // (page, position of last use)
    for (Size i = 0; i < refs.size(); ++i) {
        const PageId page = refs[i];
        bool hit = false;
        for (auto& slot : slots) {
            if (slot.first == page) {
                slot.second = i;
                hit = true;
                break;
            }
        }
        int evicted = E;
        if (!hit) {
            if (slots.size() < frames) {
                slots.emplace_back(page, i);
            } else {
                Size victim = 0;
                for (Size k = 1; k < slots.size(); ++k) {
                    if (slots[k].second < slots[victim].second) {
                        victim = k;
                    }
                }
                evicted = slots[victim].first;
                slots[victim] = {page, i};
            }
        }
        std::vector<int> expectedFrames(frames, E);
        for (Size k = 0; k < slots.size(); ++k) {
            expectedFrames[k] = slots[k].first;
        }

        const StepRecord& s = r.steps[i];
        const int gotEvicted = s.evicted.has_value() ? *s.evicted : E;
        const bool ok = s.fault == !hit && gotEvicted == evicted && snapshot(s) == expectedFrames;
        if (!ok) {
            CHECK(ok);
            std::cerr << "  frames=" << frames << " step " << i << " page " << page << ": got "
                      << show(snapshot(s)) << " evicted " << gotEvicted << ", expected "
                      << show(expectedFrames) << " evicted " << evicted << "\n";
            return;
        }
    }
}

void test_lru_matches_naive_reference_implementation() {
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u}) {
        for (const PageId range : {4, 9, 25}) {
            const auto refs = pseudoRandomRefs(seed, 400, range);
            for (Size frames = 1; frames <= 8; ++frames) {
                expectMatchesNaiveLru(frames, refs);
            }
        }
    }
}

void test_lru_is_deterministic_and_reusable() {
    LRU lru;
    const auto first = PageReplacementSimulator::run(lru, 3, kTextbookRefs);
    const auto second = PageReplacementSimulator::run(lru, 3, kTextbookRefs);
    CHECK_EQ(first.metrics.faults, second.metrics.faults);
    for (std::size_t i = 0; i < first.steps.size(); ++i) {
        CHECK(snapshot(first.steps[i]) == snapshot(second.steps[i]));
    }
    // Stale list/map state from the previous run must not leak into a new one.
    const auto other = PageReplacementSimulator::run(lru, 2, {9, 8, 9, 7});
    CHECK(other.steps[3].evicted == std::optional<PageId>(8));
    CHECK(snapshot(other.steps[3]) == std::vector<int>({9, 7}));
}

void test_lru_invalid_input_rejected() {
    LRU lru;
    CHECK_THROWS(PageReplacementSimulator::run(lru, 0, {1}), std::invalid_argument);
    CHECK_THROWS(PageReplacementSimulator::run(lru, 2, {1, -1}), std::invalid_argument);
    CHECK_EQ(lru.name(), std::string("LRU"));
}

// ---------- Optimal ----------

SimulationResult runOptimal(Size frames, const std::vector<PageId>& refs) {
    Optimal optimal;
    return PageReplacementSimulator::run(optimal, frames, refs);
}

void test_optimal_empty_reference_string() {
    const auto r = runOptimal(3, {});
    CHECK_EQ(r.steps.size(), 0);
    CHECK_EQ(r.metrics.faults, 0);
    CHECK(near(r.metrics.hitRatio, 0.0));
    CHECK(near(r.metrics.faultRatio, 0.0));
}

void test_optimal_single_frame() {
    const auto r = runOptimal(1, {1, 1, 2, 2, 1});  // no choice with one frame: F H F H F
    CHECK_EQ(r.metrics.faults, 3);
    CHECK_EQ(r.metrics.hits, 2);
    CHECK_EQ(r.metrics.replacements, 2);
}

void test_optimal_repeated_page() {
    const auto r = runOptimal(3, {5, 5, 5, 5});
    CHECK_EQ(r.metrics.faults, 1);
    CHECK_EQ(r.metrics.hits, 3);
    CHECK(snapshot(r.steps[3]) == std::vector<int>({5, E, E}));
}

void test_optimal_prefers_page_never_used_again() {
    // 1 2 3 4 1 2, 3 frames: at 4, pages 1 and 2 return soon but 3 never does, so 3 is evicted.
    const auto r = runOptimal(3, {1, 2, 3, 4, 1, 2});
    CHECK(r.steps[3].evicted == std::optional<PageId>(3));
    CHECK(snapshot(r.steps[3]) == std::vector<int>({1, 2, 4}));
    CHECK_EQ(r.metrics.faults, 4);
    CHECK_EQ(r.metrics.hits, 2);
}

void test_optimal_all_unique_ties_go_to_lowest_slot() {
    // 1..6, 3 frames: no resident page is ever used again, so every eviction is a tie
    // and the lowest slot is chosen: evicts 1 (slot 0), then 4 (slot 0), then 5 (slot 0).
    // (FIFO would evict 1, 2, 3.)
    const auto r = runOptimal(3, {1, 2, 3, 4, 5, 6});
    CHECK_EQ(r.metrics.faults, 6);
    CHECK_EQ(r.metrics.replacements, 3);
    CHECK(r.steps[3].evicted == std::optional<PageId>(1));
    CHECK(r.steps[4].evicted == std::optional<PageId>(4));
    CHECK(r.steps[5].evicted == std::optional<PageId>(5));
    CHECK(snapshot(r.steps[5]) == std::vector<int>({6, 2, 3}));
}

void test_textbook_optimal_step_by_step() {
    // Hand trace, 3 frames. Next-use indices drive each eviction (see comments).
    //  idx3  page 2: next uses 7->17, 0->4, 1->13   => evict 7
    //  idx5  page 3: next uses 2->8, 0->6, 1->13    => evict 1
    //  idx7  page 4: next uses 2->8, 0->10, 3->9    => evict 0
    //  idx10 page 0: next uses 2->12, 4->never, 3->11 => evict 4
    //  idx13 page 1: next uses 2->14, 0->15, 3->never => evict 3
    //  idx17 page 7: next uses 2->never, 0->18, 1->19 => evict 2
    const std::vector<std::vector<int>> frames = {
        {7, E, E}, {7, 0, E}, {7, 0, 1}, {2, 0, 1}, {2, 0, 1}, {2, 0, 3}, {2, 0, 3},
        {2, 4, 3}, {2, 4, 3}, {2, 4, 3}, {2, 0, 3}, {2, 0, 3}, {2, 0, 3}, {2, 0, 1},
        {2, 0, 1}, {2, 0, 1}, {2, 0, 1}, {7, 0, 1}, {7, 0, 1}, {7, 0, 1}};
    const std::vector<bool> fault = {1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0};
    const std::vector<int> evicted = {E, E, E, 7, E, 1, E, 0, E, E, 4, E, E, 3, E, E, E, 2, E, E};

    const auto r = runOptimal(3, kTextbookRefs);
    CHECK_EQ(r.steps.size(), 20);
    for (std::size_t i = 0; i < r.steps.size(); ++i) {
        const StepRecord& s = r.steps[i];
        CHECK_EQ(s.fault, fault[i]);
        const int ev = s.evicted.has_value() ? *s.evicted : E;
        CHECK_EQ(ev, evicted[i]);
        const std::vector<int> got = snapshot(s);
        if (got != frames[i]) {
            CHECK(got == frames[i]);
            std::cerr << "  step " << i << ": got " << show(got) << " expected " << show(frames[i]) << "\n";
        }
    }
    CHECK_EQ(r.metrics.faults, 9);
    CHECK_EQ(r.metrics.hits, 11);
    CHECK_EQ(r.metrics.replacements, 6);  // 9 faults - 3 that filled empty frames
    CHECK(near(r.metrics.hitRatio, 0.55));
    CHECK(near(r.metrics.faultRatio, 0.45));
}

void test_all_three_algorithms_on_textbook_string() {
    // Same input: Optimal <= LRU <= FIFO for this workload (9, 12, 15). Workload-specific.
    CHECK_EQ(runOptimal(3, kTextbookRefs).metrics.faults, 9);
    CHECK_EQ(runLru(3, kTextbookRefs).metrics.faults, 12);
    CHECK_EQ(runFifo(3, kTextbookRefs).metrics.faults, 15);
}

void test_optimal_classic_belady_string() {
    // 1 2 3 4 1 2 5 1 2 3 4 5: Optimal faults 7 with 3 frames and 6 with 4.
    const std::vector<PageId> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    const auto three = runOptimal(3, refs);
    CHECK_EQ(three.metrics.faults, 7);
    CHECK_EQ(runOptimal(4, refs).metrics.faults, 6);
    // Hand trace of the tie-breaks at the end of the 3-frame run:
    //  idx9  page 3: pages 1 and 2 are never used again (tie) => lowest slot, evict 1
    //  idx10 page 4: pages 3 and 2 never used again (tie)     => lowest slot, evict 3
    CHECK(three.steps[9].evicted == std::optional<PageId>(1));
    CHECK(snapshot(three.steps[9]) == std::vector<int>({3, 2, 5}));
    CHECK(three.steps[10].evicted == std::optional<PageId>(3));
    CHECK(snapshot(three.steps[10]) == std::vector<int>({4, 2, 5}));
}

// Exhaustive minimum number of faults over every possible eviction choice.
Size bruteForceMinFaults(const std::vector<PageId>& refs, Size capacity, Size pos,
                         std::vector<PageId> cache, std::map<std::pair<Size, std::vector<PageId>>, Size>& memo) {
    if (pos == refs.size()) {
        return 0;
    }
    std::sort(cache.begin(), cache.end());
    const auto key = std::make_pair(pos, cache);
    const auto cached = memo.find(key);
    if (cached != memo.end()) {
        return cached->second;
    }

    const PageId page = refs[pos];
    Size best = 0;
    if (std::find(cache.begin(), cache.end(), page) != cache.end()) {
        best = bruteForceMinFaults(refs, capacity, pos + 1, cache, memo);
    } else if (cache.size() < capacity) {
        std::vector<PageId> next = cache;
        next.push_back(page);
        best = 1 + bruteForceMinFaults(refs, capacity, pos + 1, next, memo);
    } else {
        best = std::numeric_limits<Size>::max();
        for (Size victim = 0; victim < cache.size(); ++victim) {
            std::vector<PageId> next = cache;
            next[victim] = page;
            best = std::min(best, 1 + bruteForceMinFaults(refs, capacity, pos + 1, next, memo));
        }
    }
    memo[key] = best;
    return best;
}

void test_optimal_matches_exhaustive_minimum() {
    for (std::uint32_t seed = 1; seed <= 12; ++seed) {
        const auto refs = pseudoRandomRefs(seed, 14, 5);
        for (Size frames = 1; frames <= 4; ++frames) {
            std::map<std::pair<Size, std::vector<PageId>>, Size> memo;
            const Size minimum = bruteForceMinFaults(refs, frames, 0, {}, memo);
            CHECK_EQ(runOptimal(frames, refs).metrics.faults, minimum);
        }
    }
}

void test_no_algorithm_beats_optimal() {
    for (const std::uint32_t seed : {11u, 12u, 13u}) {
        for (const PageId range : {6, 15}) {
            const auto refs = pseudoRandomRefs(seed, 500, range);
            for (Size frames = 1; frames <= 8; ++frames) {
                const Size opt = runOptimal(frames, refs).metrics.faults;
                CHECK(opt <= runFifo(frames, refs).metrics.faults);
                CHECK(opt <= runLru(frames, refs).metrics.faults);
            }
        }
    }
}

void test_optimal_stack_property_more_frames_never_more_faults() {
    for (const auto& refs : {kTextbookRefs, pseudoRandomRefs(5u, 500, 12), pseudoRandomRefs(6u, 500, 30)}) {
        Size previous = runOptimal(1, refs).metrics.faults;
        for (Size frames = 2; frames <= 10; ++frames) {
            const Size current = runOptimal(frames, refs).metrics.faults;
            CHECK(current <= previous);
            previous = current;
        }
    }
}

void test_optimal_is_deterministic_and_reusable() {
    Optimal optimal;
    const auto first = PageReplacementSimulator::run(optimal, 3, kTextbookRefs);
    const auto second = PageReplacementSimulator::run(optimal, 3, kTextbookRefs);
    CHECK_EQ(first.metrics.faults, second.metrics.faults);
    for (std::size_t i = 0; i < first.steps.size(); ++i) {
        CHECK(snapshot(first.steps[i]) == snapshot(second.steps[i]));
    }
    // A different reference string on the same object must not see the old future.
    const auto other = PageReplacementSimulator::run(optimal, 2, {9, 8, 7, 9});
    CHECK(other.steps[2].evicted == std::optional<PageId>(8));  // 9 returns, 8 never does
    CHECK(snapshot(other.steps[2]) == std::vector<int>({9, 7}));
}

void test_optimal_rejects_access_that_deviates_from_reset_string() {
    Optimal wrongPage;
    wrongPage.reset(2, {1, 2, 3});
    wrongPage.access(1);
    CHECK_THROWS(wrongPage.access(5), std::logic_error);  // reset() promised 2 here

    Optimal tooLong;
    tooLong.reset(2, {1});
    tooLong.access(1);
    CHECK_THROWS(tooLong.access(1), std::logic_error);  // past the end of the declared string
}

void test_optimal_invalid_input_rejected() {
    Optimal optimal;
    CHECK_THROWS(PageReplacementSimulator::run(optimal, 0, {1}), std::invalid_argument);
    CHECK_THROWS(PageReplacementSimulator::run(optimal, 2, {1, -1}), std::invalid_argument);
    CHECK_EQ(optimal.name(), std::string("Optimal"));
}

// ---------- Extreme values ----------

void test_extreme_page_numbers_and_frame_counts() {
    const std::vector<PageId> refs = {1000000000, 0, 999999999, 1000000000, 0};
    for (const Size frames : {Size{2}, Size{3}, Size{1000}}) {
        for (const SimulationResult& r : {runFifo(frames, refs), runLru(frames, refs), runOptimal(frames, refs)}) {
            CHECK_EQ(r.metrics.hits + r.metrics.faults, refs.size());
            CHECK_EQ(r.steps.back().frames.size(), frames);
        }
    }
    // 1000 frames and only 3 distinct pages: no evictions, 3 faults, 2 hits, for every algorithm.
    for (const SimulationResult& r : {runFifo(1000, refs), runLru(1000, refs), runOptimal(1000, refs)}) {
        CHECK_EQ(r.metrics.faults, 3);
        CHECK_EQ(r.metrics.hits, 2);
        CHECK_EQ(r.metrics.replacements, 0);
    }
}

void test_maximum_size_workload_is_consistent_across_algorithms() {
    // 10000 references (the CLI maximum) over 1000 frames, frame tables off to keep memory small.
    const auto refs = pseudoRandomRefs(31u, 10000, 1500);
    const PageComparison cmp = PageReplacementSimulator::compareAll(1000, refs, SimulationOptions{false});
    CHECK_EQ(cmp.results.size(), 3);
    for (const SimulationResult& r : cmp.results) {
        CHECK_EQ(r.metrics.totalReferences, 10000);
        CHECK_EQ(r.metrics.hits + r.metrics.faults, 10000);
        CHECK(r.metrics.replacements <= r.metrics.faults);
    }
    CHECK(cmp.results[2].metrics.faults <= cmp.results[0].metrics.faults);  // Optimal <= FIFO
    CHECK(cmp.results[2].metrics.faults <= cmp.results[1].metrics.faults);  // Optimal <= LRU
}

}  // namespace

int main() {
    RUN_TEST(test_empty_reference_string);
    RUN_TEST(test_single_frame);
    RUN_TEST(test_repeated_page_faults_once);
    RUN_TEST(test_all_unique_references);
    RUN_TEST(test_more_frames_than_distinct_pages);

    RUN_TEST(test_hit_does_not_protect_oldest_page);
    RUN_TEST(test_textbook_fifo_step_by_step);
    RUN_TEST(test_beladys_anomaly);
    RUN_TEST(test_metrics_are_internally_consistent);

    RUN_TEST(test_run_is_deterministic_and_reusable);
    RUN_TEST(test_zero_frames_rejected);
    RUN_TEST(test_negative_page_rejected);
    RUN_TEST(test_access_before_reset_rejected);
    RUN_TEST(test_page_zero_is_valid);
    RUN_TEST(test_compute_metrics_on_empty_trace);

    RUN_TEST(test_lru_empty_reference_string);
    RUN_TEST(test_lru_single_frame);
    RUN_TEST(test_lru_repeated_page);
    RUN_TEST(test_lru_all_unique_references);
    RUN_TEST(test_lru_hit_refreshes_recency);
    RUN_TEST(test_lru_replacement_order_with_multiple_hits);
    RUN_TEST(test_textbook_lru_step_by_step);
    RUN_TEST(test_lru_beats_fifo_on_textbook_string);
    RUN_TEST(test_lru_has_no_beladys_anomaly_on_classic_string);
    RUN_TEST(test_lru_stack_property_more_frames_never_more_faults);
    RUN_TEST(test_lru_matches_naive_reference_implementation);
    RUN_TEST(test_lru_is_deterministic_and_reusable);
    RUN_TEST(test_lru_invalid_input_rejected);

    RUN_TEST(test_optimal_empty_reference_string);
    RUN_TEST(test_optimal_single_frame);
    RUN_TEST(test_optimal_repeated_page);
    RUN_TEST(test_optimal_prefers_page_never_used_again);
    RUN_TEST(test_optimal_all_unique_ties_go_to_lowest_slot);
    RUN_TEST(test_textbook_optimal_step_by_step);
    RUN_TEST(test_all_three_algorithms_on_textbook_string);
    RUN_TEST(test_optimal_classic_belady_string);
    RUN_TEST(test_optimal_matches_exhaustive_minimum);
    RUN_TEST(test_no_algorithm_beats_optimal);
    RUN_TEST(test_optimal_stack_property_more_frames_never_more_faults);
    RUN_TEST(test_optimal_is_deterministic_and_reusable);
    RUN_TEST(test_optimal_rejects_access_that_deviates_from_reset_string);
    RUN_TEST(test_optimal_invalid_input_rejected);

    RUN_TEST(test_extreme_page_numbers_and_frame_counts);
    RUN_TEST(test_maximum_size_workload_is_consistent_across_algorithms);

    return TEST_SUMMARY();
}
