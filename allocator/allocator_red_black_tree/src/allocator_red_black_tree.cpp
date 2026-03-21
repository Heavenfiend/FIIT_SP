#include <not_implemented.h>
#include <mutex>
#include <stdexcept>

#include "../include/allocator_red_black_tree.h"

namespace
{
    using fit_mode = allocator_with_fit_mode::fit_mode;

    std::pmr::memory_resource*& get_parent_allocator(void* trusted_memory)
    {
        // родительский аллокатор хранится после стандартных метаданных
        return *reinterpret_cast<std::pmr::memory_resource**>(
                reinterpret_cast<char*>(trusted_memory) + sizeof(allocator_dbg_helper*) + sizeof(fit_mode) + sizeof(size_t) + sizeof(std::mutex) + sizeof(void*)
        );
    }

    allocator_dbg_helper*& get_dbg_helper(void* trusted_memory)
    {
        return *reinterpret_cast<allocator_dbg_helper**>(trusted_memory);
    }

    fit_mode& get_fit_mode(void* trusted_memory)
    {
        return *reinterpret_cast<fit_mode*>(
                reinterpret_cast<char*>(trusted_memory) + sizeof(allocator_dbg_helper*)
        );
    }

    size_t& get_allocator_size(void* trusted_memory)
    {
        return *reinterpret_cast<size_t*>(
                reinterpret_cast<char*>(trusted_memory) + sizeof(allocator_dbg_helper*) + sizeof(fit_mode)
        );
    }

    std::mutex* get_mutex(void* trusted_memory)
    {
        return reinterpret_cast<std::mutex*>(
                reinterpret_cast<char*>(trusted_memory) + sizeof(allocator_dbg_helper*) + sizeof(fit_mode) + sizeof(size_t)
        );
    }

    void** get_root_rb_tree(void* trusted_memory)
    {
        return reinterpret_cast<void**>(
                reinterpret_cast<char*>(trusted_memory) + sizeof(allocator_dbg_helper*) + sizeof(fit_mode) + sizeof(size_t) + sizeof(std::mutex)
        );
    }

    size_t& get_block_size(void* block_ptr)
    {
        return *reinterpret_cast<size_t*>(block_ptr);
    }

    void** get_prev_phys(void* block_ptr)
    {
        return reinterpret_cast<void**>(reinterpret_cast<char*>(block_ptr) + sizeof(size_t));
    }

    void* get_next_phys(void* block_ptr, void* trusted_memory)
    {
        size_t block_size = get_block_size(block_ptr);
        void* next_phys = reinterpret_cast<char*>(block_ptr) + block_size;

        size_t alloc_size = get_allocator_size(trusted_memory);
        if (reinterpret_cast<char*>(next_phys) >= reinterpret_cast<char*>(trusted_memory) + alloc_size)
            return nullptr;

        return next_phys;
    }

    struct block_data_t {
        bool occupied : 4;
        unsigned char color : 4;
    };

    block_data_t* get_block_data_fix(void* block_ptr)
    {
        return reinterpret_cast<block_data_t*>(reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + 2 * sizeof(void*));
    }

    void** get_parent(void* block_ptr)
    {
        return reinterpret_cast<void**>(reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + 2 * sizeof(void*) + sizeof(block_data_t));
    }

    void** get_left(void* block_ptr)
    {
        return reinterpret_cast<void**>(reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + 3 * sizeof(void*) + sizeof(block_data_t));
    }

    void** get_right(void* block_ptr)
    {
        return reinterpret_cast<void**>(reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + 4 * sizeof(void*) + sizeof(block_data_t));
    }

    constexpr unsigned char RED = 0;
    constexpr unsigned char BLACK = 1;

    // левый поворот для балансировки дерева
    void rb_left_rotate(void** root, void* x)
    {
        void* y = *get_right(x);
        *get_right(x) = *get_left(y);
        if (*get_left(y) != nullptr)
            *get_parent(*get_left(y)) = x;
        *get_parent(y) = *get_parent(x);
        if (*get_parent(x) == nullptr)
            *root = y;
        else if (x == *get_left(*get_parent(x)))
            *get_left(*get_parent(x)) = y;
        else
            *get_right(*get_parent(x)) = y;
        *get_left(y) = x;
        *get_parent(x) = y;
    }

    // правый поворот для балансировки дерева
    void rb_right_rotate(void** root, void* y)
    {
        void* x = *get_left(y);
        *get_left(y) = *get_right(x);
        if (*get_right(x) != nullptr)
            *get_parent(*get_right(x)) = y;
        *get_parent(x) = *get_parent(y);
        if (*get_parent(y) == nullptr)
            *root = x;
        else if (y == *get_right(*get_parent(y)))
            *get_right(*get_parent(y)) = x;
        else
            *get_left(*get_parent(y)) = x;
        *get_right(x) = y;
        *get_parent(y) = x;
    }

    // восстановление свойств красно-черного дерева после вставки
    void rb_insert_fixup(void** root, void* z)
    {
        while (*get_parent(z) != nullptr && get_block_data_fix(*get_parent(z))->color == RED)
        {
            if (*get_parent(z) == *get_left(*get_parent(*get_parent(z))))
            {
                void* y = *get_right(*get_parent(*get_parent(z)));
                if (y != nullptr && get_block_data_fix(y)->color == RED)
                {
                    get_block_data_fix(*get_parent(z))->color = BLACK;
                    get_block_data_fix(y)->color = BLACK;
                    get_block_data_fix(*get_parent(*get_parent(z)))->color = RED;
                    z = *get_parent(*get_parent(z));
                }
                else
                {
                    if (z == *get_right(*get_parent(z)))
                    {
                        z = *get_parent(z);
                        rb_left_rotate(root, z);
                    }
                    get_block_data_fix(*get_parent(z))->color = BLACK;
                    get_block_data_fix(*get_parent(*get_parent(z)))->color = RED;
                    rb_right_rotate(root, *get_parent(*get_parent(z)));
                }
            }
            else
            {
                void* y = *get_left(*get_parent(*get_parent(z)));
                if (y != nullptr && get_block_data_fix(y)->color == RED)
                {
                    get_block_data_fix(*get_parent(z))->color = BLACK;
                    get_block_data_fix(y)->color = BLACK;
                    get_block_data_fix(*get_parent(*get_parent(z)))->color = RED;
                    z = *get_parent(*get_parent(z));
                }
                else
                {
                    if (z == *get_left(*get_parent(z)))
                    {
                        z = *get_parent(z);
                        rb_right_rotate(root, z);
                    }
                    get_block_data_fix(*get_parent(z))->color = BLACK;
                    get_block_data_fix(*get_parent(*get_parent(z)))->color = RED;
                    rb_left_rotate(root, *get_parent(*get_parent(z)));
                }
            }
        }
        get_block_data_fix(*root)->color = BLACK;
    }

    // вставка нового блока в красно-черное дерево свободных блоков
    void rb_insert(void** root, void* z)
    {
        *get_left(z) = nullptr;
        *get_right(z) = nullptr;

        void* y = nullptr;
        void* x = *root;
        while (x != nullptr)
        {
            y = x;
            if (get_block_size(z) < get_block_size(x) || (get_block_size(z) == get_block_size(x) && z < x))
                x = *get_left(x);
            else
                x = *get_right(x);
        }
        *get_parent(z) = y;
        if (y == nullptr)
            *root = z;
        else if (get_block_size(z) < get_block_size(y) || (get_block_size(z) == get_block_size(y) && z < y))
            *get_left(y) = z;
        else
            *get_right(y) = z;
        get_block_data_fix(z)->color = RED;

        // гарантируем, что у корня нет родителя перед балансировкой
        if (*root != nullptr)
            *get_parent(*root) = nullptr;

        rb_insert_fixup(root, z);
    }

    // замена одного поддерева на другое
    void rb_transplant(void** root, void* u, void* v)
    {
        if (*get_parent(u) == nullptr)
            *root = v;
        else if (u == *get_left(*get_parent(u)))
            *get_left(*get_parent(u)) = v;
        else
            *get_right(*get_parent(u)) = v;
        if (v != nullptr)
            *get_parent(v) = *get_parent(u);
    }

    // поиск узла с минимальным размером
    void* rb_minimum(void* x)
    {
        while (*get_left(x) != nullptr)
            x = *get_left(x);
        return x;
    }

    // восстановление свойств красно-черного дерева после удаления
    void rb_delete_fixup(void** root, void* x, void* x_parent)
    {
        while (x != *root && (x == nullptr || get_block_data_fix(x)->color == BLACK))
        {
            if (x == *get_left(x_parent))
            {
                void* w = *get_right(x_parent);
                if (w != nullptr && get_block_data_fix(w)->color == RED)
                {
                    get_block_data_fix(w)->color = BLACK;
                    get_block_data_fix(x_parent)->color = RED;
                    rb_left_rotate(root, x_parent);
                    w = *get_right(x_parent);
                }
                if (w == nullptr || ((w != nullptr && (*get_left(w) == nullptr || get_block_data_fix(*get_left(w))->color == BLACK)) &&
                                     (*get_right(w) == nullptr || get_block_data_fix(*get_right(w))->color == BLACK)))
                {
                    if (w != nullptr)
                        get_block_data_fix(w)->color = RED;
                    x = x_parent;
                    x_parent = *get_parent(x);
                }
                else
                {
                    if (*get_right(w) == nullptr || get_block_data_fix(*get_right(w))->color == BLACK)
                    {
                        if (*get_left(w) != nullptr)
                            get_block_data_fix(*get_left(w))->color = BLACK;
                        get_block_data_fix(w)->color = RED;
                        rb_right_rotate(root, w);
                        w = *get_right(x_parent);
                    }
                    get_block_data_fix(w)->color = get_block_data_fix(x_parent)->color;
                    get_block_data_fix(x_parent)->color = BLACK;
                    if (*get_right(w) != nullptr)
                        get_block_data_fix(*get_right(w))->color = BLACK;
                    rb_left_rotate(root, x_parent);
                    x = *root;
                    break;
                }
            }
            else
            {
                void* w = *get_left(x_parent);
                if (w != nullptr && get_block_data_fix(w)->color == RED)
                {
                    get_block_data_fix(w)->color = BLACK;
                    get_block_data_fix(x_parent)->color = RED;
                    rb_right_rotate(root, x_parent);
                    w = *get_left(x_parent);
                }
                if (w == nullptr || ((w != nullptr && (*get_right(w) == nullptr || get_block_data_fix(*get_right(w))->color == BLACK)) &&
                                     (*get_left(w) == nullptr || get_block_data_fix(*get_left(w))->color == BLACK)))
                {
                    if (w != nullptr)
                        get_block_data_fix(w)->color = RED;
                    x = x_parent;
                    x_parent = *get_parent(x);
                }
                else
                {
                    if (*get_left(w) == nullptr || get_block_data_fix(*get_left(w))->color == BLACK)
                    {
                        if (*get_right(w) != nullptr)
                            get_block_data_fix(*get_right(w))->color = BLACK;
                        get_block_data_fix(w)->color = RED;
                        rb_left_rotate(root, w);
                        w = *get_left(x_parent);
                    }
                    get_block_data_fix(w)->color = get_block_data_fix(x_parent)->color;
                    get_block_data_fix(x_parent)->color = BLACK;
                    if (*get_left(w) != nullptr)
                        get_block_data_fix(*get_left(w))->color = BLACK;
                    rb_right_rotate(root, x_parent);
                    x = *root;
                    break;
                }
            }
        }
        if (x != nullptr)
            get_block_data_fix(x)->color = BLACK;
    }

    // удаление свободного блока из красно-черного дерева
    void rb_delete(void** root, void* z)
    {
        void* y = z;
        void* x = nullptr;
        void* x_parent = nullptr;
        unsigned char y_original_color = get_block_data_fix(y)->color;

        if (*get_left(z) == nullptr)
        {
            x = *get_right(z);
            x_parent = *get_parent(z);
            rb_transplant(root, z, *get_right(z));
        }
        else if (*get_right(z) == nullptr)
        {
            x = *get_left(z);
            x_parent = *get_parent(z);
            rb_transplant(root, z, *get_left(z));
        }
        else
        {
            y = rb_minimum(*get_right(z));
            y_original_color = get_block_data_fix(y)->color;
            x = *get_right(y);

            if (*get_parent(y) == z)
            {
                x_parent = y;
            }
            else
            {
                x_parent = *get_parent(y);
                rb_transplant(root, y, *get_right(y));
                *get_right(y) = *get_right(z);
                if (*get_right(y) != nullptr)
                    *get_parent(*get_right(y)) = y;
            }
            rb_transplant(root, z, y);
            *get_left(y) = *get_left(z);
            if (*get_left(y) != nullptr)
                *get_parent(*get_left(y)) = y;
            get_block_data_fix(y)->color = get_block_data_fix(z)->color;
        }

        if (y_original_color == BLACK)
            rb_delete_fixup(root, x, x_parent);

        // отсоединяем блок для безопасности
        *get_left(z) = nullptr;
        *get_right(z) = nullptr;
        *get_parent(z) = nullptr;
        get_block_data_fix(z)->color = RED;
    }
}

allocator_red_black_tree::~allocator_red_black_tree()
{
    if (_trusted_memory == nullptr)
        return;

    std::mutex* mtx = get_mutex(_trusted_memory);
    mtx->~mutex();

    std::pmr::memory_resource* parent = get_parent_allocator(_trusted_memory);
    size_t size = get_allocator_size(_trusted_memory);

    if (parent != nullptr) {
        parent->deallocate(_trusted_memory, size, alignof(std::max_align_t));
    } else {
        ::operator delete(_trusted_memory);
    }
    _trusted_memory = nullptr;
}

allocator_red_black_tree::allocator_red_black_tree(allocator_red_black_tree &&other) noexcept
{
    if (other._trusted_memory != nullptr)
    {
        std::lock_guard<std::mutex> lock(*get_mutex(other._trusted_memory));
        _trusted_memory = other._trusted_memory;
        get_dbg_helper(_trusted_memory) = this;
        other._trusted_memory = nullptr;
    }
    else
    {
        _trusted_memory = nullptr;
    }
}

allocator_red_black_tree &allocator_red_black_tree::operator=(allocator_red_black_tree &&other) noexcept
{
    if (this == &other)
        return *this;

    if (_trusted_memory != nullptr && other._trusted_memory != nullptr)
    {
        std::lock(*get_mutex(_trusted_memory), *get_mutex(other._trusted_memory));
        std::lock_guard<std::mutex> lock1(*get_mutex(_trusted_memory), std::adopt_lock);
        std::lock_guard<std::mutex> lock2(*get_mutex(other._trusted_memory), std::adopt_lock);

        this->~allocator_red_black_tree();
        _trusted_memory = other._trusted_memory;
        get_dbg_helper(_trusted_memory) = this;
        other._trusted_memory = nullptr;
    }
    else if (_trusted_memory != nullptr)
    {
        std::lock_guard<std::mutex> lock1(*get_mutex(_trusted_memory));
        this->~allocator_red_black_tree();
        _trusted_memory = nullptr;
    }
    else if (other._trusted_memory != nullptr)
    {
        std::lock_guard<std::mutex> lock2(*get_mutex(other._trusted_memory));
        _trusted_memory = other._trusted_memory;
        get_dbg_helper(_trusted_memory) = this;
        other._trusted_memory = nullptr;
    }

    return *this;
}

allocator_red_black_tree::allocator_red_black_tree(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    // резервируем место под дополнительный указатель (родительский аллокатор)
    size_t actual_metadata_size = allocator_metadata_size + sizeof(std::pmr::memory_resource*);

    // выравниваем размер метаданных для корректного доступа к памяти
    size_t alignment = alignof(std::max_align_t);
    actual_metadata_size = (actual_metadata_size + alignment - 1) & ~(alignment - 1);

    size_t total_size = space_size + actual_metadata_size + free_block_metadata_size;

    if (parent_allocator != nullptr) {
        _trusted_memory = parent_allocator->allocate(total_size, alignof(std::max_align_t));
    } else {
        _trusted_memory = ::operator new(total_size);
    }

    get_parent_allocator(_trusted_memory) = parent_allocator;
    get_dbg_helper(_trusted_memory) = this;
    get_fit_mode(_trusted_memory) = allocate_fit_mode;
    get_allocator_size(_trusted_memory) = total_size;
    new (get_mutex(_trusted_memory)) std::mutex();
    *get_root_rb_tree(_trusted_memory) = nullptr;

    void* first_block = reinterpret_cast<char*>(_trusted_memory) + actual_metadata_size;
    get_block_size(first_block) = space_size + free_block_metadata_size;
    *get_prev_phys(first_block) = nullptr;
    *get_parent(first_block) = nullptr;
    *get_left(first_block) = nullptr;
    *get_right(first_block) = nullptr;
    get_block_data_fix(first_block)->occupied = false;

    rb_insert(get_root_rb_tree(_trusted_memory), first_block);
}

allocator_red_black_tree::allocator_red_black_tree(const allocator_red_black_tree &other)
{
    throw std::logic_error("allocator_red_black_tree::allocator_red_black_tree(const allocator_red_black_tree &other)");
}

allocator_red_black_tree &allocator_red_black_tree::operator=(const allocator_red_black_tree &other)
{
    throw std::logic_error("allocator_red_black_tree &allocator_red_black_tree::operator=(const allocator_red_black_tree &other)");
}

bool allocator_red_black_tree::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto p = dynamic_cast<const allocator_red_black_tree*>(&other);
    return p != nullptr && p->_trusted_memory == _trusted_memory;
}

[[nodiscard]] void *allocator_red_black_tree::do_allocate_sm(size_t size)
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));

    void* current_node = *get_root_rb_tree(_trusted_memory);
    void* best_node = nullptr;

    fit_mode mode = get_fit_mode(_trusted_memory);

    size_t target_size = size + occupied_block_metadata_size;

    if (mode == fit_mode::first_fit)
    {
        size_t alignment = alignof(std::max_align_t);
        size_t actual_metadata_size = (allocator_metadata_size + sizeof(std::pmr::memory_resource*) + alignment - 1) & ~(alignment - 1);
        void* first_block = reinterpret_cast<char*>(_trusted_memory) + actual_metadata_size;
        void* it = first_block;
        while (it != nullptr)
        {
            if (!get_block_data_fix(it)->occupied && get_block_size(it) >= target_size)
            {
                best_node = it;
                break;
            }
            it = get_next_phys(it, _trusted_memory);
        }
    }
    else if (mode == fit_mode::the_best_fit)
    {
        while (current_node != nullptr)
        {
            if (get_block_size(current_node) >= target_size)
            {
                best_node = current_node;
                current_node = *get_left(current_node);
            }
            else
            {
                current_node = *get_right(current_node);
            }
        }
    }
    else if (mode == fit_mode::the_worst_fit)
    {
        while (current_node != nullptr)
        {
            best_node = current_node;
            current_node = *get_right(current_node);
        }
        if (best_node != nullptr && get_block_size(best_node) < target_size)
        {
            best_node = nullptr;
        }
    }

    if (best_node == nullptr)
    {
        throw std::bad_alloc();
    }

    rb_delete(get_root_rb_tree(_trusted_memory), best_node);

    size_t block_size = get_block_size(best_node);

    if (block_size - target_size >= free_block_metadata_size)
    {
        // разделяем блок на занятый и свободный остаток
        void* split_node = reinterpret_cast<char*>(best_node) + target_size;
        get_block_size(split_node) = block_size - target_size;
        get_block_data_fix(split_node)->occupied = false;
        *get_prev_phys(split_node) = best_node;
        *get_parent(split_node) = nullptr;
        *get_left(split_node) = nullptr;
        *get_right(split_node) = nullptr;

        void* next_phys = get_next_phys(split_node, _trusted_memory);
        if (next_phys != nullptr)
        {
            *get_prev_phys(next_phys) = split_node;
        }

        get_block_size(best_node) = target_size;
        get_block_data_fix(best_node)->occupied = true;

        rb_insert(get_root_rb_tree(_trusted_memory), split_node);
    }
    else
    {
        get_block_data_fix(best_node)->occupied = true;
    }

    size_t alignment = alignof(std::max_align_t);
    size_t offset = (sizeof(size_t) + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<char*>(best_node) + offset;
}

void allocator_red_black_tree::do_deallocate_sm(void *at)
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));

    size_t alignment = alignof(std::max_align_t);
    size_t offset = (sizeof(size_t) + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    void* block = reinterpret_cast<char*>(at) - offset;

    size_t allocator_size = get_allocator_size(_trusted_memory);
    if (block < reinterpret_cast<char*>(_trusted_memory) || reinterpret_cast<char*>(block) >= reinterpret_cast<char*>(_trusted_memory) + allocator_size)
    {
        throw std::logic_error("Deallocating block not belonging to this allocator.");
    }

    if (!get_block_data_fix(block)->occupied)
    {
        throw std::logic_error("Deallocating already free block.");
    }

    get_block_data_fix(block)->occupied = false;

    void* prev_phys = *get_prev_phys(block);
    void* next_phys = get_next_phys(block, _trusted_memory);

    // сливаем со следующим физическим блоком, если он свободен
    if (next_phys != nullptr && !get_block_data_fix(next_phys)->occupied)
    {
        rb_delete(get_root_rb_tree(_trusted_memory), next_phys);
        get_block_size(block) += get_block_size(next_phys);
        void* next_next_phys = get_next_phys(next_phys, _trusted_memory);
        if (next_next_phys != nullptr)
        {
            *get_prev_phys(next_next_phys) = block;
        }
    }

    // сливаем с предыдущим физическим блоком, если он свободен
    if (prev_phys != nullptr && !get_block_data_fix(prev_phys)->occupied)
    {
        rb_delete(get_root_rb_tree(_trusted_memory), prev_phys);
        get_block_size(prev_phys) += get_block_size(block);
        void* after_merged = get_next_phys(block, _trusted_memory);
        if (after_merged != nullptr)
        {
            *get_prev_phys(after_merged) = prev_phys;
        }
        block = prev_phys;
    }

    *get_parent(block) = nullptr;
    *get_left(block) = nullptr;
    *get_right(block) = nullptr;
    rb_insert(get_root_rb_tree(_trusted_memory), block);
}

void allocator_red_black_tree::set_fit_mode(allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));
    get_fit_mode(_trusted_memory) = mode;
}

std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info() const
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> infos;
    for (auto it = begin(); it != end(); ++it)
    {
        infos.push_back({it.size(), it.occupied()});
    }
    return infos;
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::begin() const noexcept
{
    return rb_iterator(_trusted_memory);
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::end() const noexcept
{
    return rb_iterator();
}

bool allocator_red_black_tree::rb_iterator::operator==(const allocator_red_black_tree::rb_iterator &other) const noexcept
{
    return _block_ptr == other._block_ptr;
}

bool allocator_red_black_tree::rb_iterator::operator!=(const allocator_red_black_tree::rb_iterator &other) const noexcept
{
    return _block_ptr != other._block_ptr;
}

allocator_red_black_tree::rb_iterator &allocator_red_black_tree::rb_iterator::operator++() & noexcept
{
    _block_ptr = get_next_phys(_block_ptr, _trusted);
    return *this;
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::rb_iterator::operator++(int n)
{
    rb_iterator temp = *this;
    ++(*this);
    return temp;
}

size_t allocator_red_black_tree::rb_iterator::size() const noexcept
{
    return get_block_size(_block_ptr);
}

void *allocator_red_black_tree::rb_iterator::operator*() const noexcept
{
    return _block_ptr;
}

allocator_red_black_tree::rb_iterator::rb_iterator() : _block_ptr(nullptr), _trusted(nullptr) {}

allocator_red_black_tree::rb_iterator::rb_iterator(void *trusted) : _trusted(trusted) {
    if (_trusted != nullptr) {
        size_t alignment = alignof(std::max_align_t);
        size_t actual_metadata_size = (allocator_metadata_size + sizeof(std::pmr::memory_resource*) + alignment - 1) & ~(alignment - 1);
        _block_ptr = reinterpret_cast<char*>(_trusted) + actual_metadata_size;
    }
    else
        _block_ptr = nullptr;
}

bool allocator_red_black_tree::rb_iterator::occupied() const noexcept
{
    return get_block_data_fix(_block_ptr)->occupied;
}
