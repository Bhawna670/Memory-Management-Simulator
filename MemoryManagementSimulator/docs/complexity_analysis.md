# Complexity Analysis

n = memory blocks, e = workload events, r = page references, f = frames, d = distinct pages.

## Time

| Operation | Time | Notes |
|---|---|---|
| `selectBlock` (First/Best/Worst) | O(n) | one scan; no allocation |
| `MemoryAllocator::allocate` | O(n) | owner lookup + selection + vector insert |
| `MemoryAllocator::deallocate` | O(n) | owner lookup + merge/erase |
| `FragmentationAnalyzer::analyze` | O(n) | single pass |
| `replayWorkload` | O(e·n) | analyzes memory after each event to track the peak |
| `compareAllocation` | 3 × replay | |
| FIFO per reference | O(1) | queue push/pop |
| LRU per reference | O(1) average | hash lookup + list `splice` / `push_front` / `erase`; worst case O(f) only under pathological hash collisions |
| Optimal preprocessing | O(r) | backward pass with a hash map |
| Optimal per hit / per fault | O(1) / O(f) | victim scan over resident pages |
| Base-class bookkeeping per reference | O(1) average | residency map |
| Frame snapshot per reference | O(f) | display only; optional (`recordFrames`) |
| `PageReplacementSimulator::run` | O(r) policy work + O(r·f) if frames are recorded | |
| `compareAll` | 3 × run | |

LRU's lookup, insertion and eviction are O(1) as required. A full `access()` is O(f) only because it copies the
frame table for display; with recording off it is O(1) average for FIFO and LRU.

## Space

| Structure | Space |
|---|---|
| Block list, process registry, history | O(n), O(processes), O(e) |
| FIFO / LRU state | O(f) |
| Optimal state | O(r + f) |
| Simulation trace | O(r) without frame tables, O(r·f) with them |

## Measurement
LRU `access()`, 40,000 references, microseconds per access (single run, GCC -O2; indicative):

| Frames | frames recorded | frames not recorded |
|---|---|---|
| 64 | 0.112 | 0.053 |
| 1,024 | 0.856 | 0.072 |
| 16,384 | 11.2 | 0.083 |

With recording off the cost is flat in the frame count, confirming the policy itself is O(1). Whole-program stress at the
CLI maximum (1000 frames x 10,000 references): about 0.1-0.3 s wall time (two runs: 0.11 s and 0.25 s), 84 MB peak memory, 1.3 MB of output.
