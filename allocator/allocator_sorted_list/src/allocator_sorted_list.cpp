#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"
#include <stdexcept>
#include <cstring>

namespace {

    std::pmr::memory_resource*& get_parent(void* trusted) {
        return *reinterpret_cast<std::pmr::memory_resource**>(trusted);
    }
    allocator_with_fit_mode::fit_mode& get_mode(void* trusted) {
        return *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*)
        );
    }
    size_t& get_size(void* trusted) {
        return *reinterpret_cast<size_t*>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode)
        );
    }
    std::mutex& get_mutex(void* trusted) {
        return *reinterpret_cast<std::mutex*>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t)
        );
    }
    void*& get_first_free(void* trusted) {
        return *reinterpret_cast<void**>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t) + sizeof(std::mutex)
        );
    }

    size_t& get_block_size(void* block) {
        return *reinterpret_cast<size_t*>(block);
    }
    void*& get_next_free(void* block) {
        return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(block) + sizeof(size_t));
    }
    void* get_block_payload(void* block) {
        return reinterpret_cast<uint8_t*>(block) + sizeof(size_t) + sizeof(void*);
    }
    void* get_block_from_payload(void* payload) {
        return reinterpret_cast<uint8_t*>(payload) - sizeof(size_t) - sizeof(void*);
    }

    bool is_block_occupied(void* block) {
        return get_next_free(block) == block;
    }

    void set_block_occupied(void* block) {
        get_next_free(block) = block;
    }
}

allocator_sorted_list::~allocator_sorted_list()
{
    if (!_trusted_memory) return;

    std::pmr::memory_resource* parent = get_parent(_trusted_memory);
    size_t size = get_size(_trusted_memory);

    get_mutex(_trusted_memory).~mutex();

    parent->deallocate(_trusted_memory, size);
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
    if (this == &other) return *this;
    this->~allocator_sorted_list();
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
    return *this;
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (!parent_allocator) {
        parent_allocator = std::pmr::get_default_resource();
    }

    if (space_size < allocator_metadata_size + block_metadata_size) {
        throw std::bad_alloc();
    }

    _trusted_memory = parent_allocator->allocate(space_size);

    // Clear memory to avoid uninitialized reads
    std::memset(_trusted_memory, 0, space_size);

    get_parent(_trusted_memory) = parent_allocator;
    get_mode(_trusted_memory) = allocate_fit_mode;
    get_size(_trusted_memory) = space_size;

    new (&get_mutex(_trusted_memory)) std::mutex();

    void* first_block = reinterpret_cast<uint8_t*>(_trusted_memory) + allocator_metadata_size;
    get_first_free(_trusted_memory) = first_block;

    get_block_size(first_block) = space_size - allocator_metadata_size - block_metadata_size;
    get_next_free(first_block) = nullptr;
}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    size_t size)
{
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }

    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    void* prev_block = nullptr;
    void* current_block = get_first_free(_trusted_memory);

    void* best_prev = nullptr;
    void* best_block = nullptr;

    allocator_with_fit_mode::fit_mode mode = get_mode(_trusted_memory);

    while (current_block) {
        size_t block_size = get_block_size(current_block);

        if (block_size >= size) {
            if (mode == allocator_with_fit_mode::fit_mode::first_fit) {
                best_prev = prev_block;
                best_block = current_block;
                break;
            } else if (mode == allocator_with_fit_mode::fit_mode::the_best_fit) {
                if (!best_block || block_size < get_block_size(best_block)) {
                    best_prev = prev_block;
                    best_block = current_block;
                }
            } else if (mode == allocator_with_fit_mode::fit_mode::the_worst_fit) {
                if (!best_block || block_size > get_block_size(best_block)) {
                    best_prev = prev_block;
                    best_block = current_block;
                }
            }
        }

        prev_block = current_block;
        current_block = get_next_free(current_block);
    }

    if (!best_block) {
        throw std::bad_alloc();
    }

    size_t best_size = get_block_size(best_block);

    if (best_size >= size + block_metadata_size + 1) { // plus 1 for minimum payload
        void* new_free_block = reinterpret_cast<uint8_t*>(best_block) + block_metadata_size + size;

        get_block_size(new_free_block) = best_size - block_metadata_size - size;
        get_next_free(new_free_block) = get_next_free(best_block);

        get_block_size(best_block) = size;
        set_block_occupied(best_block);

        if (best_prev) {
            get_next_free(best_prev) = new_free_block;
        } else {
            get_first_free(_trusted_memory) = new_free_block;
        }
    } else {
        if (best_prev) {
            get_next_free(best_prev) = get_next_free(best_block);
        } else {
            get_first_free(_trusted_memory) = get_next_free(best_block);
        }
    }

    set_block_occupied(best_block);

    return get_block_payload(best_block);
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
{
    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return;
    }

    std::pmr::memory_resource* parent = get_parent(other._trusted_memory);
    size_t size = get_size(other._trusted_memory);

    _trusted_memory = parent->allocate(size);
    std::memcpy(_trusted_memory, other._trusted_memory, size);

    new (&get_mutex(_trusted_memory)) std::mutex();

    void* other_first = get_first_free(other._trusted_memory);
    if (other_first) {
        get_first_free(_trusted_memory) = reinterpret_cast<uint8_t*>(_trusted_memory) +
            (reinterpret_cast<uint8_t*>(other_first) - reinterpret_cast<uint8_t*>(other._trusted_memory));

        void* current_other = other_first;
        void* current_this = get_first_free(_trusted_memory);

        while (get_next_free(current_other)) {
            void* next_other = get_next_free(current_other);
            void* next_this = reinterpret_cast<uint8_t*>(_trusted_memory) +
                (reinterpret_cast<uint8_t*>(next_other) - reinterpret_cast<uint8_t*>(other._trusted_memory));

            get_next_free(current_this) = next_this;

            current_other = next_other;
            current_this = next_this;
        }
    }
}

allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    if (this == &other) return *this;

    this->~allocator_sorted_list();

    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return *this;
    }

    std::pmr::memory_resource* parent = get_parent(other._trusted_memory);
    size_t size = get_size(other._trusted_memory);

    _trusted_memory = parent->allocate(size);
    std::memcpy(_trusted_memory, other._trusted_memory, size);

    new (&get_mutex(_trusted_memory)) std::mutex();

    void* other_first = get_first_free(other._trusted_memory);
    if (other_first) {
        get_first_free(_trusted_memory) = reinterpret_cast<uint8_t*>(_trusted_memory) +
            (reinterpret_cast<uint8_t*>(other_first) - reinterpret_cast<uint8_t*>(other._trusted_memory));

        void* current_other = other_first;
        void* current_this = get_first_free(_trusted_memory);

        while (get_next_free(current_other)) {
            void* next_other = get_next_free(current_other);
            void* next_this = reinterpret_cast<uint8_t*>(_trusted_memory) +
                (reinterpret_cast<uint8_t*>(next_other) - reinterpret_cast<uint8_t*>(other._trusted_memory));

            get_next_free(current_this) = next_this;

            current_other = next_other;
            current_this = next_this;
        }
    }

    return *this;
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto* other_allocator = dynamic_cast<const allocator_sorted_list*>(&other);
    if (!other_allocator) return false;
    return _trusted_memory == other_allocator->_trusted_memory;
}

void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (!_trusted_memory) return;

    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    void* block = get_block_from_payload(at);
    size_t size = get_size(_trusted_memory);

    if (at < reinterpret_cast<uint8_t*>(_trusted_memory) + allocator_metadata_size + block_metadata_size ||
        at >= reinterpret_cast<uint8_t*>(_trusted_memory) + size) {
        throw std::logic_error("Pointer does not belong to this allocator");
    }

    void* prev_free = nullptr;
    void* current_free = get_first_free(_trusted_memory);

    while (current_free && current_free < block) {
        prev_free = current_free;
        current_free = get_next_free(current_free);
    }

    get_next_free(block) = current_free;

    if (prev_free) {
        get_next_free(prev_free) = block;
    } else {
        get_first_free(_trusted_memory) = block;
    }

    if (current_free && reinterpret_cast<uint8_t*>(block) + block_metadata_size + get_block_size(block) == reinterpret_cast<uint8_t*>(current_free)) {
        get_block_size(block) += block_metadata_size + get_block_size(current_free);
        get_next_free(block) = get_next_free(current_free);
    }

    if (prev_free && reinterpret_cast<uint8_t*>(prev_free) + block_metadata_size + get_block_size(prev_free) == reinterpret_cast<uint8_t*>(block)) {
        get_block_size(prev_free) += block_metadata_size + get_block_size(block);
        get_next_free(prev_free) = get_next_free(block);

        // Zero out metadata of merged block to avoid dangling pointers
        std::memset(block, 0, block_metadata_size);
    }
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    if (!_trusted_memory) return;
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    get_mode(_trusted_memory) = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    if (!_trusted_memory) return {};
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> info;
    for (auto it = begin(); it != end(); ++it) {
        info.push_back({it.size(), it.occupied()});
    }
    return info;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    if (!_trusted_memory) return sorted_free_iterator();
    return sorted_free_iterator(get_first_free(_trusted_memory));
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator(nullptr);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    if (!_trusted_memory) return sorted_iterator();
    return sorted_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return sorted_iterator();
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return _free_ptr != other._free_ptr;
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr) {
        _free_ptr = get_next_free(_free_ptr);
    }
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    sorted_free_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    if (!_free_ptr) return 0;
    return get_block_size(_free_ptr);
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    return _free_ptr;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() : _free_ptr(nullptr)
{
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted) : _free_ptr(trusted)
{
}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    return _current_ptr == other._current_ptr;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return _current_ptr != other._current_ptr;
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (_current_ptr) {
        void* next_block = reinterpret_cast<uint8_t*>(_current_ptr) + block_metadata_size + get_block_size(_current_ptr);
        if (reinterpret_cast<uint8_t*>(next_block) >= reinterpret_cast<uint8_t*>(_trusted_memory) + get_size(_trusted_memory)) {
            _current_ptr = nullptr;
        } else {
            _current_ptr = next_block;
        }
    }
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    sorted_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    if (!_current_ptr) return 0;
    return get_block_size(_current_ptr);
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    return _current_ptr;
}

allocator_sorted_list::sorted_iterator::sorted_iterator() : _current_ptr(nullptr), _trusted_memory(nullptr)
{
}

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted) : _trusted_memory(trusted)
{
    if (trusted) {
        _current_ptr = reinterpret_cast<uint8_t*>(trusted) + allocator_metadata_size;
        if (_current_ptr >= reinterpret_cast<uint8_t*>(trusted) + get_size(trusted)) {
            _current_ptr = nullptr;
        }
    } else {
        _current_ptr = nullptr;
    }
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    if (!_current_ptr) return false;

    void* current_free = get_first_free(_trusted_memory);
    while (current_free && current_free < _current_ptr) {
        current_free = get_next_free(current_free);
    }

    return current_free != _current_ptr;
}
