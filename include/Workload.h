#pragma once

#include <vector>

#include "Types.h"

namespace memsim {

// One step of an allocation workload. A Workload is replayed unchanged on every
// placement algorithm so that comparisons use identical input.
struct AllocationEvent {
    enum class Kind { Allocate, Deallocate };

    Kind kind{Kind::Allocate};
    ProcessId id{0};
    Size size{0};  // used by Allocate only

    static AllocationEvent allocate(ProcessId id, Size size) {
        return AllocationEvent{Kind::Allocate, id, size};
    }
    static AllocationEvent deallocate(ProcessId id) {
        return AllocationEvent{Kind::Deallocate, id, 0};
    }
};

using Workload = std::vector<AllocationEvent>;

}  // namespace memsim
