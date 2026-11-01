# Algorithms

All results below are checked by the test suites; worked numbers were derived by hand first.

## 1. Contiguous allocation

### Memory model
Memory is an address-ordered list of blocks that always covers `[0, total)` with no gaps. Each block is `Free`
(no owner, requested size 0) or `Allocated` (owner process, `0 < requested <= size`). The block sizes you enter
become **partitions**; their start addresses are remembered and never merged across.

### Placement (selection only)
A block is *suitable* if it is free and `size >= request`. A request of 0 selects nothing.

| Strategy | Rule | Tie-break |
|---|---|---|
| First Fit | first suitable block by address | n/a |
| Best Fit | smallest suitable block | lowest address |
| Worst Fit | largest suitable block | lowest address |

### Splitting and merging
* After choosing block `B` for a request `R`, `leftover = size(B) - R`.
  If `leftover > 0` and `leftover >= splitThreshold`, `B` shrinks to `R` and a new free block of `leftover` follows it.
  Otherwise the leftover stays inside `B` and counts as **internal fragmentation**.
* `splitThreshold = 1` is pure dynamic partitioning (no internal fragmentation). **Fixed-partition mode** never splits and never merges.
* On deallocation (if merging is on) the freed block merges with a free left/right neighbour unless a partition
  boundary lies between them. Merging across your configured partitions would erase the structure being compared.

### Failure accounting
Only requests that reach block selection are *attempts*. Invalid size/ID and duplicate-process requests are rejected
earlier and counted separately. A failed attempt where total free memory >= request is flagged
(`failedDespiteEnoughFree`): that failure is caused by external fragmentation.

### Fragmentation formulas
* Internal fragmentation = sum over **allocated** blocks of (block size − requested size). Free blocks never contribute.
* External indicator = 1 − (largest free block / total free memory); defined as 0 when there is no free memory.
  It measures how scattered free space is, not whether a given request will fail.
* Utilization is reported two ways: allocated bytes / total and requested bytes / total (the gap is internal fragmentation).

### Worked example
Partitions `100 500 200 300 600`, requests 212, 417, 112, 426 (dynamic, threshold 1):

| | P1 (212) | P2 (417) | P3 (112) | P4 (426) |
|---|---|---|---|---|
| First Fit | @100 | @1100 | @312 | fails (959 free, largest hole 300) |
| Best Fit | @800 | @100 | @600 | @1100 |
| Worst Fit | @1100 | @100 | @1312 | fails (959 free, largest hole 300) |

## 2. Page replacement

### Shared mechanics
The base class keeps fixed frame slots, an O(1) `page -> slot` map and the hit/fault logic. A replaced page's slot is
reused by the incoming page; empty slots fill lowest-index first. Subclasses only choose a victim:

| Policy | Victim | Structure |
|---|---|---|
| FIFO | longest resident; hits do not change order | `std::queue` |
| LRU | least recently used; a hit moves the page to the front | `std::list` + `std::unordered_map`, `splice` |
| Optimal | next use farthest in the future, or never; ties go to the lowest slot | `nextOccurrence_` vector built in one backward pass |

### Metrics
* hits + faults = total references. **Replacements** = faults that evicted a page (the first `frames` distinct pages
  fill empty frames and are faults, not replacements).
* Hit ratio and fault ratio are 0 for an empty reference string.

### Worked results (checked in tests)
| Reference string | Frames | FIFO | LRU | Optimal |
|---|---|---|---|---|
| 7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1 | 3 | 15 faults | 12 | 9 |
| 1 2 3 4 1 2 5 1 2 3 4 5 | 3 | 9 | 10 | 7 |
| same | 4 | 10 | 8 | 6 |

FIFO shows **Belady's anomaly** on the last two rows (more frames, more faults); LRU and Optimal are stack algorithms
and cannot. Optimal is the minimum for a given string and frame count (verified against an exhaustive search), but it is
offline: it needs the future, so it serves as a benchmark.

## 3. Comparison engine
Every comparison builds fresh algorithm/allocator instances from the same configuration and feeds the same
workload or reference string. Allocation comparison replays your recorded allocate/deallocate history. Results
describe that workload only.
