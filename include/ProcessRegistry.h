#pragma once

#include <string>
#include <vector>

#include "Process.h"
#include "Types.h"

namespace memsim {

enum class RegistryStatus { Success, InvalidId, InvalidSize, DuplicateId, NotFound };

std::string toString(RegistryStatus status);

// Tracks the processes the user has defined. Enforces ID/size validity and
// uniqueness so that neither the CLI nor the allocator has to.
// Processes are kept in insertion order; lookup is linear (n is small).
class ProcessRegistry {
public:
    RegistryStatus add(ProcessId id, Size size);
    RegistryStatus setState(ProcessId id, ProcessState state);
    RegistryStatus remove(ProcessId id);

    const Process* find(ProcessId id) const noexcept;
    const std::vector<Process>& processes() const noexcept { return processes_; }
    Size count() const noexcept { return processes_.size(); }
    void clear() noexcept { processes_.clear(); }

private:
    std::vector<Process>::iterator locate(ProcessId id);

    std::vector<Process> processes_;
};

}  // namespace memsim
