#pragma once

#include <cstddef>

namespace memsim {

using ProcessId = int;          // valid IDs are > 0
using PageId = int;             // valid page numbers are >= 0
using Size = std::size_t;       // memory sizes, addresses, counts

}  // namespace memsim
