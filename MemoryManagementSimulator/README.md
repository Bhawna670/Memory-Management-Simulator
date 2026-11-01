# Memory Management Simulator

An interactive C++17 simulator of two operating-system memory-management modules:

* **Contiguous memory allocation** with First Fit, Best Fit and Worst Fit, block splitting and merging,
  deallocation, and internal/external fragmentation analysis.
* **Virtual memory / page replacement** with FIFO, LRU and Optimal (Belady's MIN) as an offline benchmark.

Both modules can be compared on identical input. The project is a command-line application plus a library
and seven test suites. It uses only the C++ standard library.

## Features

* Configurable memory blocks; **dynamic partitions** (split + merge, configurable split threshold) or **fixed partitions**.
* Add processes, allocate, deallocate, switch placement algorithm at any time, view the memory map and statistics.
* Fragmentation analysis: total/allocated/free memory, internal fragmentation, free-block count, largest free block,
  external-fragmentation indicator. Division by zero is handled everywhere.
* Page replacement with a configurable frame count and custom reference string; per-reference frame table,
  replaced page, hit/fault, hit and fault ratios, replacement count.
* Side-by-side comparison of First/Best/Worst Fit (replaying your own allocation history) and of FIFO/LRU/Optimal.
* Strict input validation (negative, zero, non-numeric, oversized values, duplicate process IDs, bad frame counts).
* Deterministic results; end of input exits cleanly.
* A built-in sample simulation (menu option 5).

## Technology Stack

C++17 (STL only) | CMake 3.13+ | GCC / Clang / MinGW-w64 (MSVC flags are present but untested) | no third-party dependencies |
a small header-only test harness (`tests/TestHarness.h`).

## Folder Structure

```
MemoryManagementSimulator/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── docs/
│   ├── algorithms.md            algorithm rules, tie-breaking, formulas, worked examples
│   └── complexity_analysis.md   time/space analysis and measurements
├── include/                     public headers (namespace memsim)
│   ├── Types.h  Process.h  MemoryBlock.h  ProcessRegistry.h
│   ├── PlacementStrategy.h  FirstFit.h  BestFit.h  WorstFit.h
│   ├── MemoryAllocator.h  FragmentationAnalyzer.h  Workload.h
│   ├── PageReplacement.h  FIFO.h  LRU.h  Optimal.h  PageReplacementSimulator.h
│   ├── PerformanceAnalyzer.h
│   └── InputValidator.h  TablePrinter.h  CliApp.h
├── src/                         one .cpp per header, plus main.cpp
└── tests/
    ├── TestHarness.h
    ├── allocation_tests.cpp       blocks, process registry, block-list validation
    ├── placement_tests.cpp        First/Best/Worst Fit selection
    ├── allocator_tests.cpp        allocate/deallocate/split/merge/accounting, fragmentation
    ├── page_replacement_tests.cpp FIFO, LRU, Optimal
    ├── performance_tests.cpp      comparison engine
    ├── cli_tests.cpp              validator, printer, scripted sessions, fuzzing
    └── edge_case_tests.cpp        status texts, misuse, arithmetic limits
```

The spec's suggested layout was extended with `PlacementStrategy`/`ProcessRegistry`/`Workload`/`PageReplacementSimulator`
and a CLI layer (`InputValidator`, `TablePrinter`, `CliApp`) so each class has one responsibility and algorithms
stay testable without the UI.

## Algorithms

Full rules and worked examples are in [docs/algorithms.md](docs/algorithms.md).

* **First Fit** picks the lowest-address free block that fits; **Best Fit** the smallest that fits;
  **Worst Fit** the largest. Ties go to the lowest address, so results are deterministic.
* **Splitting:** if the leftover after placing a process is at least the *split threshold*, it becomes a new
  free block; otherwise it stays inside the allocated block as internal fragmentation.
* **Merging:** on deallocation, adjacent free blocks merge, but never across the original partition boundaries you configured.
* **FIFO** evicts the page resident longest; **LRU** the least recently used; **Optimal** the page whose next use is
  farthest in the future (or never). Optimal needs the whole reference string, so it is a benchmark, not a real policy.
* Internal fragmentation = allocated block size − requested size (allocated blocks only).
  External indicator = 1 − largest free block / total free memory (0 when nothing is free).

## Data Structures

| Structure | Used for |
|---|---|
| `std::vector<MemoryBlock>` (address-ordered) | the memory map; splitting and merging |
| `std::set<Size>` | initial partition boundaries (merge never crosses them) |
| Strategy pattern (`PlacementStrategy`) | First/Best/Worst Fit share one allocator |
| `std::queue` | FIFO arrival order |
| `std::list` + `std::unordered_map` | LRU: O(1) lookup, touch, insert, evict (`splice`) |
| `std::vector` (`nextOccurrence_`) + `unordered_map` | Optimal's future-reference analysis |
| Template-method base class (`PageReplacementAlgorithm`) | shared frame slots, residency map, hit/fault bookkeeping |

## Complexity

n = memory blocks, r = references, f = frames, e = workload events. Details and measurements in
[docs/complexity_analysis.md](docs/complexity_analysis.md).

| Operation | Time |
|---|---|
| First/Best/Worst Fit selection, allocate, deallocate, fragmentation analysis | O(n) |
| FIFO, LRU per reference (policy work) | O(1) average |
| Optimal: preprocessing / per hit / per fault | O(r) / O(1) / O(f) |
| Frame table snapshot per reference (optional, display only) | O(f) |

## Build

Requires a C++17 compiler and CMake 3.13 or newer.

```bash
cmake -S . -B build
cmake --build build
```

MinGW on Windows:

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

No CMake? Any GCC/MinGW works:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Iinclude src/*.cpp -o memsim
```

Verified here with GCC 13.3 and CMake 4.4.4 on Linux with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`:
no warnings. The MinGW and MSVC paths have not been tested in the author's environment.

## Run

```bash
./build/memsim            # Windows: build\memsim.exe
```

Main menu: 1 Memory Allocation, 2 Page Replacement, 3 Fragmentation Analysis, 4 Performance Comparison,
5 Run Sample Simulation, 6 Exit. Type `cancel` at any prompt to go back. Block sizes, frame counts and reference
strings accept spaces or commas. Limits: values up to 1,000,000,000; up to 1000 blocks, 1000 frames,
10,000 references. In the page table only the first 16 frame slots are shown per row for wide frame counts.

## Sample Input / Output

### Memory allocation (partitions 100 500 200 300 600; processes 212, 417, 112, 426)

Inputs, one per line: `1` (Memory Allocation), `1` (Initialize), `100 500 200 300 600`, blank, blank (defaults:
dynamic partitions, split threshold 1); `2` (Add process) with `1`/`212`, `2`/`417`, `3`/`112`, `4`/`426`;
`4` (Allocate) with `1`, `2`, `3`, `4`; `6` (Display memory map).

```
P1 allocated at address 100 using First Fit (block size 212, internal fragmentation 0).
P2 allocated at address 1100 using First Fit (block size 417, internal fragmentation 0).
P3 allocated at address 312 using First Fit (block size 112, internal fragmentation 0).
  Allocation failed: no free block can hold 426 units.
  959 units are free in total, but not in one piece (external fragmentation).

| # | Start | End  | Size | State     | Process | Requested | Internal frag |
+---+-------+------+------+-----------+---------+-----------+---------------+
| 0 |     0 |   99 |  100 | Free      | -       |         - |             - |
| 1 |   100 |  311 |  212 | Allocated | P1      |       212 |             0 |
| 2 |   312 |  423 |  112 | Allocated | P3      |       112 |             0 |
| 3 |   424 |  599 |  176 | Free      | -       |         - |             - |
| 4 |   600 |  799 |  200 | Free      | -       |         - |             - |
| 5 |   800 | 1099 |  300 | Free      | -       |         - |             - |
| 6 |  1100 | 1516 |  417 | Allocated | P2      |       417 |             0 |
| 7 |  1517 | 1699 |  183 | Free      | -       |         - |             - |
+---+-------+------+------+-----------+---------+-----------+---------------+
Total 1700   Allocated 741   Free 959
```

### Page replacement (LRU, 3 frames, reference string 7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1)

Inputs: `2` (Page Replacement); `1` then `3` (frames); `2` then the reference string; `3` then `2` (select LRU);
`4` (Run simulation); `6` (Compare FIFO, LRU and Optimal).

```
LRU with 3 frame(s)   (first 6 of 20 rows shown)
+------+------+---------+--------+----------+
| Step | Page | Frames  | Result | Replaced |
+------+------+---------+--------+----------+
|    1 |    7 | [7 - -] | FAULT  |        - |
|    2 |    0 | [7 0 -] | FAULT  |        - |
|    3 |    1 | [7 0 1] | FAULT  |        - |
|    4 |    2 | [2 0 1] | FAULT  |        7 |
|    5 |    0 | [2 0 1] | HIT    |        - |
|    6 |    3 | [2 0 3] | FAULT  |        1 |
...

+-----------+--------+------+--------------+-----------+-------------+----------------------+
| Algorithm | Faults | Hits | Replacements | Hit ratio | Fault ratio | Faults above Optimal |
+-----------+--------+------+--------------+-----------+-------------+----------------------+
| FIFO      |     15 |    5 |           12 |    25.00% |      75.00% |                    6 |
| LRU       |     12 |    8 |            9 |    40.00% |      60.00% |                    3 |
| Optimal   |      9 |   11 |            6 |    55.00% |      45.00% |                    0 |
+-----------+--------+------+--------------+-----------+-------------+----------------------+
```

### Sample simulation (main menu option 5)

```
Memory blocks: 100 500 200 300 600   Events replayed: 4   Split threshold: 1   Merge on free
+---------------------------------+-----------+----------+-----------+
| Metric                          | First Fit | Best Fit | Worst Fit |
+---------------------------------+-----------+----------+-----------+
| Total processes (attempts)      |         4 |        4 |         4 |
| Successfully allocated          |         3 |        4 |         3 |
| Failed allocations              |         1 |        0 |         1 |
|   failed with enough total free |         1 |        0 |         1 |
| Allocation success rate         |    75.00% |  100.00% |    75.00% |
| Utilization (allocated)         |    43.59% |   68.65% |    43.59% |
| Utilization (requested)         |    43.59% |   68.65% |    43.59% |
| Peak allocated memory           |       741 |     1167 |       741 |
| Internal fragmentation          |         0 |        0 |         0 |
| Free blocks                     |         5 |        5 |         5 |
| Largest free block              |       300 |      174 |       300 |
| External fragmentation ind.     |     0.687 |    0.674 |     0.687 |
| Rejected requests               |         0 |        0 |         0 |
| Deallocation misses             |         0 |        0 |         0 |
+---------------------------------+-----------+----------+-----------+

FIFO, 3 frames: 9 faults
FIFO, 4 frames: 10 faults
```

All numbers above are copied from a real run of the built executable.

## Testing

```bash
cd build && ctest --output-on-failure        # 7 suites
cmake -S . -B build-san -DMEMSIM_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug   # AddressSanitizer + UBSan (not on MinGW)
```

At release: 205 tests / 9,686 checks, all passing; clean under ASan, UBSan and libstdc++ debug mode; zero warnings
with an extended GCC warning set; 97.4% line coverage (gcov). Expected values in tests are hand-derived from textbook
traces (e.g. the 20-reference string gives 15 FIFO, 12 LRU and 9 Optimal faults with 3 frames), cross-checked against
independent reference implementations (a naive LRU, an exhaustive minimum-fault search) and fuzzed through the real CLI.
Mutation checks (deliberately breaking the code) confirmed the tests fail when they should.

## Limitations and Future Improvements

* Memory is a single abstract address space measured in units; no compaction, no process lifetimes or timing.
* Allocation and page replacement are separate modules (no per-process page tables or multiprogramming model).
* Block lists use linear scans (fine for the supported 1000 blocks; a balanced tree or free lists would scale further).
* Optimal is offline by nature. Other policies (Clock, LFU, Second Chance) and buddy/segmentation schemes are not included.
* When a workload reuses a process ID after a failed request, algorithms can see different duplicate-request counts
  (documented in `PerformanceAnalyzer::compareAllocation`).
* Comparisons describe one workload only; no algorithm is claimed best in general.
* No save/load of sessions; text UI only. Windows/MinGW and MSVC builds are untested here.
