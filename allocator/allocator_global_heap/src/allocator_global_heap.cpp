#include "../include/allocator_global_heap.h"

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

// деструктор
allocator_global_heap::~allocator_global_heap()
{
}

// создаёт новый аллокатор как копию другого
allocator_global_heap::allocator_global_heap(const allocator_global_heap &other) :
    allocator_dbg_helper(other), smart_mem_resource(other)
{
}

allocator_global_heap &allocator_global_heap::operator=(const allocator_global_heap &other)
{
    if (this == &other)
        return *this;

    // копируем состояние базовых классов
    allocator_dbg_helper::operator=(other);
    smart_mem_resource::operator=(other);

    // возвращаем текущий объект
    return *this;
}

// функция проверки равенства memory_resource
bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto p = dynamic_cast<const allocator_global_heap*>(&other);

    return p != nullptr;
}

// move-конструктор
allocator_global_heap::allocator_global_heap(allocator_global_heap &&other) noexcept :
    allocator_dbg_helper(std::move(other)), smart_mem_resource(std::move(other))
{
}

allocator_global_heap &allocator_global_heap::operator=(allocator_global_heap &&other) noexcept
{
    if (this == &other)
        return *this;

    // перемещаем состояние базовых классов
    allocator_dbg_helper::operator=(std::move(other));
    smart_mem_resource::operator=(std::move(other));

    return *this;
}