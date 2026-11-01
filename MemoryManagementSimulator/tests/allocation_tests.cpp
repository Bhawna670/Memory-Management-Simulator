// Allocation-module tests. Checkpoint 2 covers block and process data models;
// allocator tests are appended in later checkpoints.

#include <stdexcept>
#include <vector>

#include "MemoryBlock.h"
#include "ProcessRegistry.h"
#include "TestHarness.h"

using namespace memsim;

namespace {

// ---------- MemoryBlock ----------

void test_free_block_factory() {
    const MemoryBlock b = MemoryBlock::makeFree(100, 50);
    CHECK(b.isFree());
    CHECK_EQ(b.start, 100);
    CHECK_EQ(b.size, 50);
    CHECK_EQ(b.end(), 150);
    CHECK(!b.owner.has_value());
    CHECK_EQ(b.requestedSize, 0);
}

void test_allocated_block_factory() {
    const MemoryBlock b = MemoryBlock::makeAllocated(0, 100, 7, 60);
    CHECK(!b.isFree());
    CHECK(b.owner.has_value());
    CHECK_EQ(*b.owner, 7);
    CHECK_EQ(b.requestedSize, 60);
    CHECK_EQ(b.end(), 100);
}

void test_internal_fragmentation_of_allocated_block() {
    CHECK_EQ(MemoryBlock::makeAllocated(0, 100, 1, 60).internalFragmentation(), 40);
    CHECK_EQ(MemoryBlock::makeAllocated(0, 100, 1, 100).internalFragmentation(), 0);  // exact fit
}

void test_free_block_has_no_internal_fragmentation() {
    CHECK_EQ(MemoryBlock::makeFree(0, 500).internalFragmentation(), 0);
}

// ---------- makeInitialBlocks ----------

void test_initial_blocks_are_contiguous_and_free() {
    const auto blocks = makeInitialBlocks({100, 500, 200});
    CHECK_EQ(blocks.size(), 3);
    CHECK_EQ(blocks[0].start, 0);
    CHECK_EQ(blocks[1].start, 100);
    CHECK_EQ(blocks[2].start, 600);
    CHECK_EQ(blocks[2].end(), 800);
    for (const auto& b : blocks) {
        CHECK(b.isFree());
    }
}

void test_initial_blocks_reject_empty() {
    CHECK_THROWS(makeInitialBlocks({}), std::invalid_argument);
}

void test_initial_blocks_reject_zero_size() {
    CHECK_THROWS(makeInitialBlocks({100, 0, 50}), std::invalid_argument);
}

void test_initial_blocks_reject_overflow() {
    const Size big = static_cast<Size>(-1);
    CHECK_THROWS(makeInitialBlocks({big, 1}), std::invalid_argument);
}

// ---------- validateBlockList ----------

void test_validate_accepts_initial_layout() {
    const auto blocks = makeInitialBlocks({100, 500, 200});
    const auto result = validateBlockList(blocks, 800);
    CHECK(result.valid);
    CHECK(result.message.empty());
}

void test_validate_accepts_mixed_layout() {
    const std::vector<MemoryBlock> blocks = {
        MemoryBlock::makeAllocated(0, 100, 1, 80),
        MemoryBlock::makeFree(100, 50),
        MemoryBlock::makeAllocated(150, 50, 2, 50),
    };
    CHECK(validateBlockList(blocks, 200).valid);
}

void test_validate_rejects_gap() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeFree(0, 100),
                                             MemoryBlock::makeFree(110, 50)};
    CHECK(!validateBlockList(blocks, 160).valid);
}

void test_validate_rejects_overlap() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeFree(0, 100),
                                             MemoryBlock::makeFree(90, 50)};
    CHECK(!validateBlockList(blocks, 140).valid);
}

void test_validate_rejects_nonzero_first_start() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeFree(10, 100)};
    CHECK(!validateBlockList(blocks, 100).valid);
}

void test_validate_rejects_wrong_total() {
    const auto blocks = makeInitialBlocks({100, 100});
    CHECK(!validateBlockList(blocks, 300).valid);
    CHECK(!validateBlockList(blocks, 100).valid);
}

void test_validate_rejects_zero_size_block() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeFree(0, 0)};
    CHECK(!validateBlockList(blocks, 0).valid);
}

void test_validate_rejects_free_block_with_owner() {
    MemoryBlock b = MemoryBlock::makeFree(0, 100);
    b.owner = 3;
    CHECK(!validateBlockList({b}, 100).valid);
}

void test_validate_rejects_free_block_with_requested_size() {
    MemoryBlock b = MemoryBlock::makeFree(0, 100);
    b.requestedSize = 10;
    CHECK(!validateBlockList({b}, 100).valid);
}

void test_validate_rejects_allocated_block_without_owner() {
    MemoryBlock b = MemoryBlock::makeAllocated(0, 100, 1, 50);
    b.owner.reset();
    CHECK(!validateBlockList({b}, 100).valid);
}

void test_validate_rejects_requested_larger_than_block() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeAllocated(0, 100, 1, 101)};
    CHECK(!validateBlockList(blocks, 100).valid);
}

void test_validate_rejects_allocated_with_zero_requested() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeAllocated(0, 100, 1, 0)};
    CHECK(!validateBlockList(blocks, 100).valid);
}

void test_validate_rejects_duplicate_owner() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeAllocated(0, 100, 5, 100),
                                             MemoryBlock::makeAllocated(100, 100, 5, 100)};
    CHECK(!validateBlockList(blocks, 200).valid);
}

void test_validate_message_identifies_block() {
    const std::vector<MemoryBlock> blocks = {MemoryBlock::makeFree(0, 100),
                                             MemoryBlock::makeAllocated(100, 50, 1, 60)};
    const auto result = validateBlockList(blocks, 150);
    CHECK(!result.valid);
    CHECK(result.message.find("block 1") != std::string::npos);
}

// Registry lookups that report a missing process as a test failure instead of dereferencing null.
Size sizeOf(const ProcessRegistry& registry, ProcessId id) {
    const Process* process = registry.find(id);
    CHECK(process != nullptr);
    return process != nullptr ? process->size : 0;
}

ProcessState stateOf(const ProcessRegistry& registry, ProcessId id) {
    const Process* process = registry.find(id);
    CHECK(process != nullptr);
    return process != nullptr ? process->state : ProcessState::Failed;
}

// ---------- ProcessRegistry ----------

void test_registry_add_and_find() {
    ProcessRegistry reg;
    CHECK(reg.add(1, 212) == RegistryStatus::Success);
    CHECK_EQ(reg.count(), 1);
    const Process* p = reg.find(1);
    CHECK(p != nullptr);
    if (p != nullptr) {
        CHECK_EQ(p->size, 212);
        CHECK(p->state == ProcessState::Pending);
    }
    CHECK(reg.find(2) == nullptr);
}

void test_registry_rejects_invalid_id() {
    ProcessRegistry reg;
    CHECK(reg.add(0, 10) == RegistryStatus::InvalidId);
    CHECK(reg.add(-4, 10) == RegistryStatus::InvalidId);
    CHECK_EQ(reg.count(), 0);
}

void test_registry_rejects_zero_size() {
    ProcessRegistry reg;
    CHECK(reg.add(1, 0) == RegistryStatus::InvalidSize);
    CHECK_EQ(reg.count(), 0);
}

void test_registry_rejects_duplicate_id() {
    ProcessRegistry reg;
    CHECK(reg.add(1, 10) == RegistryStatus::Success);
    CHECK(reg.add(1, 99) == RegistryStatus::DuplicateId);
    CHECK_EQ(reg.count(), 1);
    CHECK_EQ(sizeOf(reg, 1), 10);  // original untouched
}

void test_registry_set_state() {
    ProcessRegistry reg;
    reg.add(1, 10);
    CHECK(reg.setState(1, ProcessState::Allocated) == RegistryStatus::Success);
    CHECK(stateOf(reg, 1) == ProcessState::Allocated);
    CHECK(reg.setState(9, ProcessState::Failed) == RegistryStatus::NotFound);
}

void test_registry_remove_allows_id_reuse() {
    ProcessRegistry reg;
    reg.add(1, 10);
    CHECK(reg.remove(1) == RegistryStatus::Success);
    CHECK(reg.find(1) == nullptr);
    CHECK(reg.remove(1) == RegistryStatus::NotFound);
    CHECK(reg.add(1, 20) == RegistryStatus::Success);
    CHECK_EQ(sizeOf(reg, 1), 20);
}

void test_registry_preserves_insertion_order() {
    ProcessRegistry reg;
    reg.add(3, 1);
    reg.add(1, 1);
    reg.add(2, 1);
    reg.remove(1);
    const auto& list = reg.processes();
    CHECK_EQ(list.size(), 2);
    CHECK_EQ(list[0].id, 3);
    CHECK_EQ(list[1].id, 2);
}

void test_registry_clear() {
    ProcessRegistry reg;
    reg.add(1, 1);
    reg.add(2, 1);
    reg.clear();
    CHECK_EQ(reg.count(), 0);
    CHECK(reg.find(1) == nullptr);
}

}  // namespace

int main() {
    RUN_TEST(test_free_block_factory);
    RUN_TEST(test_allocated_block_factory);
    RUN_TEST(test_internal_fragmentation_of_allocated_block);
    RUN_TEST(test_free_block_has_no_internal_fragmentation);

    RUN_TEST(test_initial_blocks_are_contiguous_and_free);
    RUN_TEST(test_initial_blocks_reject_empty);
    RUN_TEST(test_initial_blocks_reject_zero_size);
    RUN_TEST(test_initial_blocks_reject_overflow);

    RUN_TEST(test_validate_accepts_initial_layout);
    RUN_TEST(test_validate_accepts_mixed_layout);
    RUN_TEST(test_validate_rejects_gap);
    RUN_TEST(test_validate_rejects_overlap);
    RUN_TEST(test_validate_rejects_nonzero_first_start);
    RUN_TEST(test_validate_rejects_wrong_total);
    RUN_TEST(test_validate_rejects_zero_size_block);
    RUN_TEST(test_validate_rejects_free_block_with_owner);
    RUN_TEST(test_validate_rejects_free_block_with_requested_size);
    RUN_TEST(test_validate_rejects_allocated_block_without_owner);
    RUN_TEST(test_validate_rejects_requested_larger_than_block);
    RUN_TEST(test_validate_rejects_allocated_with_zero_requested);
    RUN_TEST(test_validate_rejects_duplicate_owner);
    RUN_TEST(test_validate_message_identifies_block);

    RUN_TEST(test_registry_add_and_find);
    RUN_TEST(test_registry_rejects_invalid_id);
    RUN_TEST(test_registry_rejects_zero_size);
    RUN_TEST(test_registry_rejects_duplicate_id);
    RUN_TEST(test_registry_set_state);
    RUN_TEST(test_registry_remove_allows_id_reuse);
    RUN_TEST(test_registry_preserves_insertion_order);
    RUN_TEST(test_registry_clear);

    return TEST_SUMMARY();
}
