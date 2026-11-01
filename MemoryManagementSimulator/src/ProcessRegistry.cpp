#include "ProcessRegistry.h"

#include <algorithm>

namespace memsim {

std::string toString(ProcessState state) {
    switch (state) {
        case ProcessState::Pending:   return "Pending";
        case ProcessState::Allocated: return "Allocated";
        case ProcessState::Failed:    return "Failed";
    }
    return "Unknown";
}

std::string toString(RegistryStatus status) {
    switch (status) {
        case RegistryStatus::Success:     return "Success";
        case RegistryStatus::InvalidId:   return "Process ID must be a positive integer";
        case RegistryStatus::InvalidSize: return "Process size must be greater than 0";
        case RegistryStatus::DuplicateId: return "A process with this ID already exists";
        case RegistryStatus::NotFound:    return "No process with this ID exists";
    }
    return "Unknown";
}

std::vector<Process>::iterator ProcessRegistry::locate(ProcessId id) {
    return std::find_if(processes_.begin(), processes_.end(),
                        [id](const Process& p) { return p.id == id; });
}

RegistryStatus ProcessRegistry::add(ProcessId id, Size size) {
    if (id <= 0) {
        return RegistryStatus::InvalidId;
    }
    if (size == 0) {
        return RegistryStatus::InvalidSize;
    }
    if (find(id) != nullptr) {
        return RegistryStatus::DuplicateId;
    }
    processes_.push_back(Process{id, size, ProcessState::Pending});
    return RegistryStatus::Success;
}

RegistryStatus ProcessRegistry::setState(ProcessId id, ProcessState state) {
    const auto it = locate(id);
    if (it == processes_.end()) {
        return RegistryStatus::NotFound;
    }
    it->state = state;
    return RegistryStatus::Success;
}

RegistryStatus ProcessRegistry::remove(ProcessId id) {
    const auto it = locate(id);
    if (it == processes_.end()) {
        return RegistryStatus::NotFound;
    }
    processes_.erase(it);
    return RegistryStatus::Success;
}

const Process* ProcessRegistry::find(ProcessId id) const noexcept {
    const auto it = std::find_if(processes_.begin(), processes_.end(),
                                 [id](const Process& p) { return p.id == id; });
    return it == processes_.end() ? nullptr : &*it;
}

}  // namespace memsim
