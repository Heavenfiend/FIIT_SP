#include "../include/allocator_global_heap.h"
#include <new> // подключаем new чтобы использовать operator new и operator delete

allocator_global_heap::allocator_global_heap()
{
}

[[nodiscard]] void *allocator_global_heap::do_allocate_sm(
    size_t size)
{
    // блокируем mutex чтобы несколько потоков не выделяли память одновременно
    std::lock_guard<std::mutex> lock(_mutex);

    // выделяем память из глобальной кучи
    return ::operator new(size);
}

void allocator_global_heap::do_deallocate_sm(
    void *at)
{
    // блокируем mutex чтобы освобождение памяти было потокобезопасным
    std::lock_guard<std::mutex> lock(_mutex);

    // освобождаем ранее выделенную память
    ::operator delete(at);
}

// деструктор, здесь ничего делать не нужно
allocator_global_heap::~allocator_global_heap()
{
}

// конструктор копирования
// создаёт новый аллокатор как копию другого
allocator_global_heap::allocator_global_heap(const allocator_global_heap &other) :
    allocator_dbg_helper(other), smart_mem_resource(other) // копируем базовые классы
{
}

allocator_global_heap &allocator_global_heap::operator=(const allocator_global_heap &other)
{
    // проверяем чтобы не было самоприсваивания (например a = a)
    if (this == &other)
        return *this;

    // копируем состояние базовых классов
    allocator_dbg_helper::operator=(other);
    smart_mem_resource::operator=(other);

    // возвращаем текущий объект
    return *this;
}

// функция проверки равенства memory_resource
// используется в системе std::pmr
bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    // пытаемся привести другой ресурс к типу allocator_global_heap
    auto p = dynamic_cast<const allocator_global_heap*>(&other);

    // если приведение удалось значит это тот же тип аллокатора
    return p != nullptr;
}

// move-конструктор
// используется когда объект перемещается вместо копирования
allocator_global_heap::allocator_global_heap(allocator_global_heap &&other) noexcept :
    allocator_dbg_helper(std::move(other)), smart_mem_resource(std::move(other)) // перемещаем базовые классы
{
}

allocator_global_heap &allocator_global_heap::operator=(allocator_global_heap &&other) noexcept
{
    // проверка на самоприсваивание
    if (this == &other)
        return *this;

    // перемещаем состояние базовых классов
    allocator_dbg_helper::operator=(std::move(other));
    smart_mem_resource::operator=(std::move(other));

    return *this;
}