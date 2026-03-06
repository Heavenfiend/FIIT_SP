#include "../include/allocator_global_heap.h"
#include <new>

allocator_global_heap::allocator_global_heap()
{
}

[[nodiscard]] void *allocator_global_heap::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(_mutex);
    return ::operator new(size);
}

void allocator_global_heap::do_deallocate_sm(
    void *at)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ::operator delete(at);
}

allocator_global_heap::~allocator_global_heap()
{
}

allocator_global_heap::allocator_global_heap(const allocator_global_heap &other) :
    allocator_dbg_helper(other), smart_mem_resource(other)
{
    // Mutex is not copied
}

allocator_global_heap &allocator_global_heap::operator=(const allocator_global_heap &other)
{
    if (this == &other)
        return *this;
    allocator_dbg_helper::operator=(other);
    smart_mem_resource::operator=(other);
    // Mutex is not copied
    return *this;
}

bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto p = dynamic_cast<const allocator_global_heap*>(&other);
    return p != nullptr;
}

allocator_global_heap::allocator_global_heap(allocator_global_heap &&other) noexcept :
    allocator_dbg_helper(std::move(other)), smart_mem_resource(std::move(other))
{
    // Mutex is not moved
}

allocator_global_heap &allocator_global_heap::operator=(allocator_global_heap &&other) noexcept
{
    if (this == &other)
        return *this;
    allocator_dbg_helper::operator=(std::move(other));
    smart_mem_resource::operator=(std::move(other));
    // Mutex is not moved
    return *this;
}
