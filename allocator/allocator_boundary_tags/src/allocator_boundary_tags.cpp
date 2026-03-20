#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"
#include <stdexcept>

struct allocator_meta {
    std::pmr::memory_resource* parent_allocator;
    allocator_with_fit_mode::fit_mode allocate_fit_mode;
    size_t space_size;
    alignas(std::mutex) std::mutex mutex;
    void* first_block;
};

inline std::pmr::memory_resource*& get_parent_allocator(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->parent_allocator;
}

inline allocator_with_fit_mode::fit_mode& get_fit_mode(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->allocate_fit_mode;
}

inline size_t& get_space_size(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->space_size;
}

inline std::mutex& get_mutex(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->mutex;
}

inline void*& get_first_block(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->first_block;
}

inline size_t& get_block_size(void* block_ptr) {
    return *reinterpret_cast<size_t*>(block_ptr);
}

inline void*& get_trusted_memory_ptr(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t)
    );
}

inline void*& get_next_block_ptr(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + sizeof(void*)
    );
}

inline void*& get_prev_block_ptr(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + sizeof(void*) * 2
    );
}

allocator_boundary_tags::~allocator_boundary_tags()
{
    if (!_trusted_memory) return;

    std::mutex& mtx = get_mutex(_trusted_memory);
    mtx.~mutex();

    std::pmr::memory_resource* parent_allocator = get_parent_allocator(_trusted_memory);
    size_t total_size = get_space_size(_trusted_memory);

    parent_allocator->deallocate(_trusted_memory, total_size);
}

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept
{
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
    if (this == &other) return *this;

    if (_trusted_memory) {
        this->~allocator_boundary_tags();
    }

    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;

    return *this;
}


/** If parent_allocator* == nullptr you should use std::pmr::get_default_resource()
 */
allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator == nullptr) {
        parent_allocator = std::pmr::get_default_resource();
    }

    size_t total_size = space_size + sizeof(allocator_meta);
    // Note: allocator_metadata_size might be slightly different than sizeof(allocator_meta)
    // due to alignment, but we must use sizeof(allocator_meta) for our struct.
    _trusted_memory = parent_allocator->allocate(total_size);

    get_parent_allocator(_trusted_memory) = parent_allocator;
    get_fit_mode(_trusted_memory) = allocate_fit_mode;
    get_space_size(_trusted_memory) = total_size;
    new (&get_mutex(_trusted_memory)) std::mutex();

    void* first_block = reinterpret_cast<char*>(_trusted_memory) + sizeof(allocator_meta);
    get_first_block(_trusted_memory) = first_block;

    if (space_size >= occupied_block_metadata_size) {
        get_block_size(first_block) = space_size - occupied_block_metadata_size;
        get_trusted_memory_ptr(first_block) = nullptr; // nullptr means free
    } else {
        get_block_size(first_block) = 0;
        get_trusted_memory_ptr(first_block) = _trusted_memory; // Not usable
    }
    get_next_block_ptr(first_block) = nullptr;
    get_prev_block_ptr(first_block) = nullptr;
}

[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    auto fit_mode = get_fit_mode(_trusted_memory);
    void* current_block = get_first_block(_trusted_memory);
    void* target_block = nullptr;

    size_t needed_size = size;

    while (current_block != nullptr) {
        if (get_trusted_memory_ptr(current_block) == nullptr) { // block is free
            size_t current_size = get_block_size(current_block);
            if (current_size >= needed_size) {
                if (fit_mode == allocator_with_fit_mode::fit_mode::first_fit) {
                    target_block = current_block;
                    break;
                } else if (fit_mode == allocator_with_fit_mode::fit_mode::the_best_fit) {
                    if (target_block == nullptr || current_size < get_block_size(target_block)) {
                        target_block = current_block;
                    }
                } else if (fit_mode == allocator_with_fit_mode::fit_mode::the_worst_fit) {
                    if (target_block == nullptr || current_size > get_block_size(target_block)) {
                        target_block = current_block;
                    }
                }
            }
        }
        current_block = get_next_block_ptr(current_block);
    }

    if (target_block == nullptr) {
        throw std::bad_alloc();
    }

    size_t target_size = get_block_size(target_block);
    // Determine if we can split the block
    if (target_size >= needed_size + occupied_block_metadata_size) {
        void* new_block = reinterpret_cast<char*>(target_block) + occupied_block_metadata_size + needed_size;

        // Ensure new block size is recorded correctly
        get_block_size(new_block) = target_size - needed_size - occupied_block_metadata_size;
        get_trusted_memory_ptr(new_block) = nullptr;
        get_next_block_ptr(new_block) = get_next_block_ptr(target_block);
        get_prev_block_ptr(new_block) = target_block;

        if (get_next_block_ptr(target_block) != nullptr) {
            get_prev_block_ptr(get_next_block_ptr(target_block)) = new_block;
        }
        get_next_block_ptr(target_block) = new_block;
        get_block_size(target_block) = needed_size;
    } else {
        // If we don't split, we just give the whole block. Update needed_size to be the whole block size
        // so that we don't return an incorrect inner size. But the allocated size is technically `target_size`.
        needed_size = target_size;
    }

    get_trusted_memory_ptr(target_block) = _trusted_memory; // Mark as occupied
    return reinterpret_cast<char*>(target_block) + occupied_block_metadata_size;
}

void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    if (at == nullptr) return;

    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    void* target_block = reinterpret_cast<char*>(at) - occupied_block_metadata_size;

    if (get_trusted_memory_ptr(target_block) != _trusted_memory) {
        throw std::logic_error("Attempted to deallocate memory not owned by this allocator");
    }

    get_trusted_memory_ptr(target_block) = nullptr;

    void* next_block = get_next_block_ptr(target_block);
    if (next_block != nullptr && get_trusted_memory_ptr(next_block) == nullptr) {
        get_block_size(target_block) += get_block_size(next_block) + occupied_block_metadata_size;
        get_next_block_ptr(target_block) = get_next_block_ptr(next_block);
        if (get_next_block_ptr(next_block) != nullptr) {
            get_prev_block_ptr(get_next_block_ptr(next_block)) = target_block;
        }
    }

    void* prev_block = get_prev_block_ptr(target_block);
    if (prev_block != nullptr && get_trusted_memory_ptr(prev_block) == nullptr) {
        get_block_size(prev_block) += get_block_size(target_block) + occupied_block_metadata_size;
        get_next_block_ptr(prev_block) = get_next_block_ptr(target_block);
        if (get_next_block_ptr(target_block) != nullptr) {
            get_prev_block_ptr(get_next_block_ptr(target_block)) = prev_block;
        }
    }
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    get_fit_mode(_trusted_memory) = mode;
}


std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    return get_blocks_info_inner();
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    return boundary_iterator(_trusted_memory);
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    return boundary_iterator(nullptr);
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    void* current = get_first_block(_trusted_memory);

    while (current != nullptr) {
        bool occupied = get_trusted_memory_ptr(current) != nullptr;
        size_t bs = get_block_size(current);
        result.push_back({
            .block_size = bs + (occupied ? occupied_block_metadata_size : occupied_block_metadata_size),
            .is_block_occupied = occupied
        });
        current = get_next_block_ptr(current);
    }
    return result;
}

allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags &other)
{
    throw std::logic_error("Copying allocator is not supported");
}

allocator_boundary_tags &allocator_boundary_tags::operator=(const allocator_boundary_tags &other)
{
    throw std::logic_error("Copying allocator is not supported");
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto* other_alloc = dynamic_cast<const allocator_boundary_tags*>(&other);
    return other_alloc != nullptr && _trusted_memory == other_alloc->_trusted_memory;
}

bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return _occupied_ptr != other._occupied_ptr;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (_occupied_ptr) {
        _occupied_ptr = get_next_block_ptr(_occupied_ptr);
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (_occupied_ptr) {
        _occupied_ptr = get_prev_block_ptr(_occupied_ptr);
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    boundary_iterator temp = *this;
    ++(*this);
    return temp;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    boundary_iterator temp = *this;
    --(*this);
    return temp;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    if (!_occupied_ptr) return 0;
    return get_block_size(_occupied_ptr);
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    if (!_occupied_ptr) return false;
    return get_trusted_memory_ptr(_occupied_ptr) != nullptr;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    if (!_occupied_ptr) return nullptr;
    return reinterpret_cast<char*>(_occupied_ptr) + occupied_block_metadata_size;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator()
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr)
{
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted)
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(trusted)
{
    if (trusted != nullptr) {
        _occupied_ptr = get_first_block(trusted);
    }
}

void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}
