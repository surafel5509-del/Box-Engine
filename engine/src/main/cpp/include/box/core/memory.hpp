#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>

#include "box/core/assert.hpp"

namespace box::core {

/**
 * A simple monotonic allocator for short-lived frame and loading allocations.
 * reset() invalidates all returned memory and intentionally does not run destructors.
 */
class ArenaAllocator final {
public:
    explicit ArenaAllocator(std::size_t capacity)
        : storage_(capacity == 0 ? nullptr : std::make_unique<std::byte[]>(capacity)),
          capacity_(capacity) {}

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) = delete;
    ArenaAllocator& operator=(ArenaAllocator&&) = delete;

    [[nodiscard]] void* allocate(std::size_t size,
                                 std::size_t alignment = alignof(std::max_align_t)) noexcept {
        if (size == 0 || storage_ == nullptr || alignment == 0 || offset_ > capacity_) {
            return nullptr;
        }

        void* candidate = storage_.get() + offset_;
        std::size_t available = capacity_ - offset_;
        void* aligned = std::align(alignment, size, candidate, available);
        if (aligned == nullptr) {
            return nullptr;
        }

        auto* start = storage_.get();
        auto* result = static_cast<std::byte*>(aligned);
        offset_ = static_cast<std::size_t>(result - start) + size;
        return aligned;
    }

    template <typename T, typename... Args>
    [[nodiscard]] T* make(Args&&... args) {
        static_assert(!std::is_array_v<T>, "ArenaAllocator::make does not create arrays.");
        void* memory = allocate(sizeof(T), alignof(T));
        if (memory == nullptr) {
            return nullptr;
        }
        return ::new (memory) T(static_cast<Args&&>(args)...);
    }

    void reset() noexcept { offset_ = 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t used() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return capacity_ - offset_; }

private:
    std::unique_ptr<std::byte[]> storage_;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
};

/**
 * Fixed-size pool allocator with O(1) allocation and deallocation.
 * All blocks are aligned to std::max_align_t, suitable for ordinary C++ objects.
 */
class PoolAllocator final {
public:
    PoolAllocator(std::size_t blockSize, std::size_t blockCount) { initialize(blockSize, blockCount); }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;

    void initialize(std::size_t blockSize, std::size_t blockCount) {
        BOX_ASSERT(blockSize > 0, "Pool block size must be positive.");
        BOX_ASSERT(blockCount > 0, "Pool block count must be positive.");
        BOX_ASSERT(blockCount <= static_cast<std::size_t>(-1) / blockSize,
                   "Pool allocation size overflow.");

        constexpr std::size_t alignment = alignof(std::max_align_t);
        const std::size_t minimumSize = std::max(blockSize, sizeof(FreeBlock));
        BOX_ASSERT(minimumSize <= static_cast<std::size_t>(-1) - (alignment - 1),
                   "Pool block size overflow.");
        blockSize_ = (minimumSize + alignment - 1) & ~(alignment - 1);
        BOX_ASSERT(blockCount <= static_cast<std::size_t>(-1) / blockSize_,
                   "Pool allocation size overflow.");
        blockCount_ = blockCount;
        const std::size_t totalBytes = blockSize_ * blockCount_;
        const std::size_t wordCount =
            (totalBytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
        storage_ = std::make_unique<std::max_align_t[]>(wordCount);
        allocatedBlocks_ = std::make_unique<std::uint8_t[]>(blockCount_);
        std::fill_n(allocatedBlocks_.get(), blockCount_, std::uint8_t{0});
        freeList_ = nullptr;

        auto* base = reinterpret_cast<std::byte*>(storage_.get());
        for (std::size_t index = 0; index < blockCount_; ++index) {
            auto* block = reinterpret_cast<FreeBlock*>(base + index * blockSize_);
            block->next = freeList_;
            freeList_ = block;
        }
        allocatedCount_ = 0;
    }

    [[nodiscard]] void* allocate() noexcept {
        if (freeList_ == nullptr) {
            return nullptr;
        }
        FreeBlock* block = freeList_;
        freeList_ = block->next;
        const auto* base = reinterpret_cast<const std::byte*>(storage_.get());
        const auto* address = reinterpret_cast<const std::byte*>(block);
        const std::size_t index = static_cast<std::size_t>(address - base) / blockSize_;
        BOX_ASSERT(index < blockCount_ && allocatedBlocks_[index] == 0,
                   "Pool freelist contains an invalid or allocated block.");
        allocatedBlocks_[index] = 1;
        ++allocatedCount_;
        return block;
    }

    void deallocate(void* pointer) noexcept {
        if (pointer == nullptr) {
            return;
        }
        auto* base = reinterpret_cast<std::byte*>(storage_.get());
        auto* address = static_cast<std::byte*>(pointer);
        const std::size_t totalBytes = blockSize_ * blockCount_;
        const std::uintptr_t baseAddress = reinterpret_cast<std::uintptr_t>(base);
        const std::uintptr_t blockAddress = reinterpret_cast<std::uintptr_t>(address);
        BOX_ASSERT(blockAddress >= baseAddress && blockAddress - baseAddress < totalBytes,
                   "Pool pointer is outside this allocator.");
        const std::size_t offset = static_cast<std::size_t>(blockAddress - baseAddress);
        BOX_ASSERT(offset % blockSize_ == 0, "Pool pointer does not reference a block start.");
        const std::size_t index = offset / blockSize_;
        BOX_ASSERT(allocatedBlocks_[index] != 0, "Pool block was deallocated more than once.");

        auto* block = static_cast<FreeBlock*>(pointer);
        block->next = freeList_;
        freeList_ = block;
        allocatedBlocks_[index] = 0;
        --allocatedCount_;
    }

    [[nodiscard]] std::size_t blockSize() const noexcept { return blockSize_; }
    [[nodiscard]] std::size_t blockCount() const noexcept { return blockCount_; }
    [[nodiscard]] std::size_t allocatedCount() const noexcept { return allocatedCount_; }
    [[nodiscard]] std::size_t freeCount() const noexcept { return blockCount_ - allocatedCount_; }

private:
    struct FreeBlock {
        FreeBlock* next;
    };

    std::unique_ptr<std::max_align_t[]> storage_;
    std::unique_ptr<std::uint8_t[]> allocatedBlocks_;
    FreeBlock* freeList_ = nullptr;
    std::size_t blockSize_ = 0;
    std::size_t blockCount_ = 0;
    std::size_t allocatedCount_ = 0;
};

}  // namespace box::core
