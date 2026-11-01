#include "FIFO.h"

namespace memsim {

std::string FIFO::name() const { return "FIFO"; }

void FIFO::onReset(const std::vector<PageId>&) { arrivalOrder_ = std::queue<PageId>{}; }

void FIFO::onHit(PageId, Size) {}  // arrival order is unaffected by hits

void FIFO::onInsert(PageId page, Size) { arrivalOrder_.push(page); }

void FIFO::onEvict(PageId) { arrivalOrder_.pop(); }

PageId FIFO::selectVictim(Size) const { return arrivalOrder_.front(); }

}  // namespace memsim
