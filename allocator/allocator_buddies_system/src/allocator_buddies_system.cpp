#include <not_implemented.h>
#include <cstddef>
#include "../include/allocator_buddies_system.h"

namespace {
    inline size_t get_parent_allocator_offset() {
        size_t offset = sizeof(std::mutex) + sizeof(allocator_dbg_helper*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(unsigned char);
        return (offset + 7) & ~7;
    }

    inline size_t get_mem_start_offset() {
        size_t offset = get_parent_allocator_offset() + sizeof(std::pmr::memory_resource*);
        return (offset + 7) & ~7;
    }

    inline std::mutex* get_mutex(void* ptr) { return reinterpret_cast<std::mutex*>(ptr); }
    inline allocator_dbg_helper** get_logger(void* ptr) { return reinterpret_cast<allocator_dbg_helper**>(reinterpret_cast<char*>(ptr) + sizeof(std::mutex)); }
    inline allocator_with_fit_mode::fit_mode* get_fit_mode(void* ptr) { return reinterpret_cast<allocator_with_fit_mode::fit_mode*>(reinterpret_cast<char*>(ptr) + sizeof(std::mutex) + sizeof(allocator_dbg_helper*)); }
    inline unsigned char* get_k_size(void* ptr) { return reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(ptr) + sizeof(std::mutex) + sizeof(allocator_dbg_helper*) + sizeof(allocator_with_fit_mode::fit_mode)); }

    inline std::pmr::memory_resource** get_parent_allocator(void* ptr) {
        return reinterpret_cast<std::pmr::memory_resource**>(reinterpret_cast<char*>(ptr) + get_parent_allocator_offset());
    }

    inline char* get_mem_start(void* ptr) {
        return reinterpret_cast<char*>(ptr) + get_mem_start_offset();
    }
}

allocator_buddies_system::~allocator_buddies_system()
{
    if (!_trusted_memory)
        return;

    auto* mutex = get_mutex(_trusted_memory);
    mutex->~mutex();

    auto* parent_allocator = *get_parent_allocator(_trusted_memory);
    unsigned char k_size = *get_k_size(_trusted_memory);

    size_t total_size = (get_mem_start(_trusted_memory) - reinterpret_cast<char*>(_trusted_memory)) + (1ULL << k_size);

    if (parent_allocator)
        parent_allocator->deallocate(_trusted_memory, total_size);
    else
        ::operator delete(_trusted_memory);
}

allocator_buddies_system::allocator_buddies_system(
    allocator_buddies_system &&other) noexcept
    : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_buddies_system &allocator_buddies_system::operator=(
    allocator_buddies_system &&other) noexcept
{
    if (this == &other)
        return *this;

    if (_trusted_memory)
        this->~allocator_buddies_system();

    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
    return *this;
}

allocator_buddies_system::allocator_buddies_system(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    size_t offset = get_mem_start_offset();
    size_t required_size = offset + free_block_metadata_size;
    if (space_size < required_size) {
        throw std::logic_error("Space size too small");
    }

    size_t k_size = __detail::nearest_greater_k_of_2(space_size);

    size_t total_size = offset + (1ULL << k_size);

    if (parent_allocator)
        _trusted_memory = parent_allocator->allocate(total_size);
    else
        _trusted_memory = ::operator new(total_size);

    new (get_mutex(_trusted_memory)) std::mutex();
    *get_logger(_trusted_memory) = nullptr;
    *get_fit_mode(_trusted_memory) = allocate_fit_mode;
    *get_k_size(_trusted_memory) = static_cast<unsigned char>(k_size);
    *get_parent_allocator(_trusted_memory) = parent_allocator;

    auto *first_block = reinterpret_cast<block_metadata*>(get_mem_start(_trusted_memory));
    first_block->occupied = false;
    first_block->size = static_cast<unsigned char>(k_size);
}

[[nodiscard]] void *allocator_buddies_system::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));

    size_t required_size = size + occupied_block_metadata_size;
    unsigned char required_k = static_cast<unsigned char>(__detail::nearest_greater_k_of_2(required_size));
    if (required_k < min_k) required_k = static_cast<unsigned char>(min_k);

    fit_mode mode = *get_fit_mode(_trusted_memory);

    block_metadata* best_block = nullptr;

    for (buddy_iterator it = begin(); it != end(); ++it) {
        if (it.occupied()) continue;

        unsigned char current_k = reinterpret_cast<block_metadata*>(*it)->size;
        if (current_k >= required_k) {
            if (mode == fit_mode::first_fit) {
                best_block = reinterpret_cast<block_metadata*>(*it);
                break;
            } else if (mode == fit_mode::the_best_fit) {
                if (!best_block || current_k < best_block->size) {
                    best_block = reinterpret_cast<block_metadata*>(*it);
                }
            } else if (mode == fit_mode::the_worst_fit) {
                if (!best_block || current_k > best_block->size) {
                    best_block = reinterpret_cast<block_metadata*>(*it);
                }
            }
        }
    }

    if (!best_block) {
        throw std::bad_alloc();
    }

    while (best_block->size > required_k) {
        best_block->size--;
        block_metadata* buddy = reinterpret_cast<block_metadata*>(
            reinterpret_cast<char*>(best_block) + (1ULL << best_block->size));
        buddy->occupied = false;
        buddy->size = best_block->size;
    }

    best_block->occupied = true;
    *reinterpret_cast<void**>(reinterpret_cast<char*>(best_block) + sizeof(block_metadata)) = _trusted_memory;
    return reinterpret_cast<char*>(best_block) + occupied_block_metadata_size;
}

void allocator_buddies_system::do_deallocate_sm(void *at)
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));

    char* target_ptr = reinterpret_cast<char*>(at) - occupied_block_metadata_size;
    block_metadata* block = reinterpret_cast<block_metadata*>(target_ptr);

    char* mem_start = get_mem_start(_trusted_memory);
    unsigned char k_size = *get_k_size(_trusted_memory);
    char* mem_end = mem_start + (1ULL << k_size);

    if (target_ptr < mem_start || target_ptr >= mem_end) {
        throw std::logic_error("Attempt to deallocate memory not allocated by this allocator");
    }

    if (block->size >= min_k) {
        if (*reinterpret_cast<void**>(reinterpret_cast<char*>(block) + sizeof(block_metadata)) != _trusted_memory) {
            throw std::logic_error("Attempt to deallocate memory not allocated by this allocator");
        }
    }

    block->occupied = false;

    while (block->size < k_size) {
        size_t relative_offset = reinterpret_cast<char*>(block) - mem_start;
        size_t buddy_offset = relative_offset ^ (1ULL << block->size);
        block_metadata* buddy = reinterpret_cast<block_metadata*>(mem_start + buddy_offset);

        if (buddy->size == block->size && !buddy->occupied) {
            if (buddy_offset < relative_offset) {
                block = buddy;
            }
            block->size++;
        } else {
            break;
        }
    }
}

allocator_buddies_system::allocator_buddies_system(const allocator_buddies_system &other)
{
    throw std::logic_error("Copying allocator_buddies_system is not supported.");
}

allocator_buddies_system &allocator_buddies_system::operator=(const allocator_buddies_system &other)
{
    throw std::logic_error("Copying allocator_buddies_system is not supported.");
}

bool allocator_buddies_system::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto* other_buddies = dynamic_cast<const allocator_buddies_system*>(&other);
    return other_buddies && _trusted_memory == other_buddies->_trusted_memory;
}

inline void allocator_buddies_system::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));
    *get_fit_mode(_trusted_memory) = mode;
}


std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info() const noexcept
{
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> info;
    buddy_iterator end_it = end();
    for (buddy_iterator it = begin(); it != end_it; ++it) {
        allocator_test_utils::block_info block;
        block.block_size = it.size();
        block.is_block_occupied = it.occupied();
        info.push_back(block);
    }
    return info;
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::begin() const noexcept
{
    return buddy_iterator(get_mem_start(_trusted_memory));
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::end() const noexcept
{
    unsigned char k_size = *get_k_size(_trusted_memory);
    return buddy_iterator(get_mem_start(_trusted_memory) + (1ULL << k_size));
}

bool allocator_buddies_system::buddy_iterator::operator!=(const allocator_buddies_system::buddy_iterator &other) const noexcept
{
    return _block != other._block;
}

size_t allocator_buddies_system::buddy_iterator::size() const noexcept
{
    unsigned char k = reinterpret_cast<block_metadata*>(_block)->size;
    return 1ULL << k;
}

bool allocator_buddies_system::buddy_iterator::operator==(const allocator_buddies_system::buddy_iterator &other) const noexcept
{
    return _block == other._block;
}


allocator_buddies_system::buddy_iterator &allocator_buddies_system::buddy_iterator::operator++() & noexcept
{
    if (_block) {
        unsigned char k_size = reinterpret_cast<block_metadata*>(_block)->size;
        _block = reinterpret_cast<char*>(_block) + (1ULL << k_size);
    }
    return *this;
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::buddy_iterator::operator++(int n)
{
    buddy_iterator temp = *this;
    ++(*this);
    return temp;
}


bool allocator_buddies_system::buddy_iterator::occupied() const noexcept
{
    return reinterpret_cast<block_metadata*>(_block)->occupied;
}

void *allocator_buddies_system::buddy_iterator::operator*() const noexcept
{
    return _block;
}

allocator_buddies_system::buddy_iterator::buddy_iterator(void *start)
    : _block(start)
{
}

allocator_buddies_system::buddy_iterator::buddy_iterator()
    : _block(nullptr)
{
}
