#pragma once

#include <string>

#include "Types.h"

namespace memsim {

enum class ProcessState { Pending, Allocated, Failed };

struct Process {
    ProcessId id{0};
    Size size{0};  // requested memory, must be > 0
    ProcessState state{ProcessState::Pending};
};

std::string toString(ProcessState state);

}  // namespace memsim
