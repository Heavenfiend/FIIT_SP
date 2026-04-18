#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"
#include <stdexcept>
#include <vector>

struct allocator_meta {
    std::pmr::memory_resource* parent_allocator;
    allocator_with_fit_mode::fit_mode allocate_fit_mode;
    size_t space_size;
    alignas(std::mutex) std::mutex mutex;
    void* first_occupied; // ТЕПЕРЬ ТУТ ПЕРВЫЙ ЗАНЯТЫЙ БЛОК
};
// достаем указатель на родительский аллокатор
inline std::pmr::memory_resource*& get_parent_allocator(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->parent_allocator;
}
// узнаем режим поиска
inline allocator_with_fit_mode::fit_mode& get_fit_mode(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->allocate_fit_mode;
}
// узнаем общий размер всей выделенной кучи
inline size_t& get_space_size(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->space_size;
}
// достаем мьютекс
inline std::mutex& get_mutex(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->mutex;
}
// получаем указатель на самый первый блок в цепочке
inline void*& get_first_occupied(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->first_occupied;
}
// читаем размер текущего блока
inline size_t& get_block_size(void* block_ptr) {
    return *reinterpret_cast<size_t*>(block_ptr);
}
// указатель на родителя
inline void*& get_trusted_memory_ptr(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t)
    );
}
// получаем адрес соседа справа отступ 16 байт
inline void*& get_next_occupied(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + sizeof(void*)
    );
}
// получаем адрес соседа слева отступ 24 байт
inline void*& get_prev_occupied(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + sizeof(void*) * 2
    );
}
// деструктор: очишаемся и отдаем память родителю
allocator_boundary_tags::~allocator_boundary_tags()
{
    if (!_trusted_memory) return;

    std::mutex& mtx = get_mutex(_trusted_memory);
    mtx.~mutex();

    std::pmr::memory_resource* parent_allocator = get_parent_allocator(_trusted_memory);
    size_t total_size = get_space_size(_trusted_memory);

    parent_allocator->deallocate(_trusted_memory, total_size);
}
// конструктор перемещения
allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept : _trusted_memory(nullptr)
{
    if (other._trusted_memory) {
        std::lock_guard<std::mutex> lock(get_mutex(other._trusted_memory));
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
}
// перемещающее присваивание
allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
    if (this == &other) return *this;

    this->~allocator_boundary_tags();

    if (other._trusted_memory) {
        std::lock_guard<std::mutex> lock(get_mutex(other._trusted_memory));
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    } else {
        _trusted_memory = nullptr;
    }

    return *this;
}


// создаем аллокатор
allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    // если родитель не указан, берем стандартный системный ресурс
    if (parent_allocator == nullptr) {
        parent_allocator = std::pmr::get_default_resource();
    }
    // считаем общий размер
    size_t total_size = space_size + sizeof(allocator_meta);

    _trusted_memory = parent_allocator->allocate(total_size);
    // заполняем метаданными
    get_parent_allocator(_trusted_memory) = parent_allocator;
    get_fit_mode(_trusted_memory) = allocate_fit_mode;
    get_space_size(_trusted_memory) = total_size;
    new (&get_mutex(_trusted_memory)) std::mutex();
    get_first_occupied(_trusted_memory) = nullptr;
}

// здесь мы ищем подходящий свободный кусок и, если он слишком большой, отрезаем от него нужную часть
[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    // мутекс
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    auto fit_mode = get_fit_mode(_trusted_memory);
    // правая граница
    void* curr = get_first_occupied(_trusted_memory);
    // левая граница
    char* last_end = reinterpret_cast<char*>(_trusted_memory) + sizeof(allocator_meta);
    // адрес дырки
    void* target_block = nullptr;
    // правый сосед будущий
    void* best_right_neighbor = nullptr;
    size_t best_gap_size = 0;
    bool found = false;
    // лямбда, мы можем менять сверху поля
    // проверяем дырку
    auto check_gap = [&](char* gap_start, void* right_neighbor, size_t gap_size) {
        // проверяем на то влезет ли данные в блок свободный
        if (gap_size >= size + occupied_block_metadata_size) {
            // выбираем по стратегиям (first, best или worst fit)
            if (!found) {
                target_block = gap_start;
                best_right_neighbor = right_neighbor;
                best_gap_size = gap_size;
                found = true;
            } else {
                if (fit_mode == allocator_with_fit_mode::fit_mode::the_best_fit && gap_size < best_gap_size) {
                    target_block = gap_start;
                    best_right_neighbor = right_neighbor;
                    best_gap_size = gap_size;
                } else if (fit_mode == allocator_with_fit_mode::fit_mode::the_worst_fit && gap_size > best_gap_size) {
                    target_block = gap_start;
                    best_right_neighbor = right_neighbor;
                    best_gap_size = gap_size;
                }
            }
        }
    };
    // обходим занятые
    while (curr != nullptr) {
        size_t gap = reinterpret_cast<char*>(curr) - last_end;
        check_gap(last_end, curr, gap);

        if (found && fit_mode == allocator_with_fit_mode::fit_mode::first_fit) break;

        last_end = reinterpret_cast<char*>(curr) + occupied_block_metadata_size + get_block_size(curr);
        curr = get_next_occupied(curr);
    }
    // проверка после последнего занятого блока
    if (!found || fit_mode != allocator_with_fit_mode::fit_mode::first_fit) {
        char* pool_end = reinterpret_cast<char*>(_trusted_memory) + get_space_size(_trusted_memory);
        size_t final_gap = pool_end - last_end;
        check_gap(last_end, nullptr, final_gap);
    }

    // если прошли всю память и ничего не нашли — кидаем ошибку
    if (target_block == nullptr) {
        throw std::bad_alloc();
    }

    void* new_block = target_block;
    size_t actual_payload_size = size;
    if (best_gap_size < size + occupied_block_metadata_size * 2) {
        actual_payload_size = best_gap_size - occupied_block_metadata_size;
    }

    get_block_size(new_block) = actual_payload_size;

    // помечаем блок как занятый
    get_trusted_memory_ptr(new_block) = _trusted_memory;
    get_next_occupied(new_block) = best_right_neighbor;

    // вклинваем между текущим и следующим
    void* prev_occ = nullptr;
    // есть правый занятый
    if (best_right_neighbor != nullptr) {
        prev_occ = get_prev_occupied(best_right_neighbor);
        get_prev_occupied(best_right_neighbor) = new_block;
        // если нет правого занятого
    } else {
        void* temp = get_first_occupied(_trusted_memory);
        while (temp && get_next_occupied(temp)) temp = get_next_occupied(temp);
        prev_occ = temp;
    }

    get_prev_occupied(new_block) = prev_occ;

    if (prev_occ != nullptr) {
        get_next_occupied(prev_occ) = new_block;
    } else {
        get_first_occupied(_trusted_memory) = new_block;
    }

    return reinterpret_cast<char*>(new_block) + occupied_block_metadata_size;
}

// освобождение памяти: мгновенная склейка соседей
void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    // если прислали пустой указатель
    if (at == nullptr) return;
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    // попадаем в шапку блока
    void* target_block = reinterpret_cast<char*>(at) - occupied_block_metadata_size;

    // проверяем на принадлежность этому блоку
    if (get_trusted_memory_ptr(target_block) != _trusted_memory) {
        throw std::logic_error("Attempted to deallocate memory not owned by this allocator");
    }

    // помечаем как свободный (он теперь просто исчезнет из списка)
    get_trusted_memory_ptr(target_block) = nullptr;

    void* prev_occ = get_prev_occupied(target_block);
    void* next_occ = get_next_occupied(target_block);
    if (prev_occ != nullptr) {
        get_next_occupied(prev_occ) = next_occ;
    } else {
        get_first_occupied(_trusted_memory) = next_occ;
    }

    if (next_occ != nullptr) {
        get_prev_occupied(next_occ) = prev_occ;
    }
}

// меняем способ обхода
inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    get_fit_mode(_trusted_memory) = mode;
}

// получения статистики
std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    return get_blocks_info_inner();
}

// возвращает итератор, указывающий на самый первый блок памяти
allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    return boundary_iterator(_trusted_memory);
}

// возвращает итератор-пустышку
allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    return boundary_iterator(nullptr); // Исправлено на nullptr чтобы end() работал как надо
}

// функция сборки статистики: идет по всем блокам подряд и записывает в массив их размер и статус (занят/свободен)
std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    if (!_trusted_memory) return result;

    char* current_ptr = reinterpret_cast<char*>(_trusted_memory) + sizeof(allocator_meta);
    char* pool_end = reinterpret_cast<char*>(_trusted_memory) + get_space_size(_trusted_memory);
    void* curr_occ = get_first_occupied(_trusted_memory);

    while (current_ptr < pool_end) {
        if (curr_occ && current_ptr == reinterpret_cast<char*>(curr_occ)) {
            size_t bs = get_block_size(curr_occ);
            result.push_back({
                .block_size = bs + occupied_block_metadata_size,
                .is_block_occupied = true
            });
            current_ptr += occupied_block_metadata_size + bs;
            curr_occ = get_next_occupied(curr_occ);
        } else {
            // пустой блок кончится либо на начале след занятого либо в конце пула
            char* next_boundary = curr_occ ? reinterpret_cast<char*>(curr_occ) : pool_end;
            size_t gap_size = next_boundary - current_ptr;
            result.push_back({
                .block_size = gap_size,
                .is_block_occupied = false
            });
            current_ptr = next_boundary;
        }
    }
    return result;
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto* other_alloc = dynamic_cast<const allocator_boundary_tags*>(&other);
    return other_alloc != nullptr && _trusted_memory == other_alloc->_trusted_memory;
}

// сравнение итераторов: указывают ли они на один и тот же блок
bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr && _occupied == other._occupied;
}

// просто читаем адрес левого соседа
bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return !(*this == other);
}

// проходим по всему с начала
allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (!_trusted_memory || !_occupied_ptr) return *this;
    char* pool_end = reinterpret_cast<char*>(_trusted_memory) + get_space_size(_trusted_memory);
    if (_occupied_ptr == pool_end) return *this;

    if (_occupied) {
        char* gap_start = reinterpret_cast<char*>(_occupied_ptr) + occupied_block_metadata_size + get_block_size(_occupied_ptr);
        void* next_occ = get_next_occupied(_occupied_ptr);
        char* right_limit = next_occ ? reinterpret_cast<char*>(next_occ) : pool_end;

        if (gap_start < right_limit) {
            _occupied_ptr = gap_start;
            _occupied = false;
        } else {
            _occupied_ptr = right_limit;
            _occupied = (_occupied_ptr != pool_end);
        }
    } else {
        void* curr = get_first_occupied(_trusted_memory);
        while (curr && reinterpret_cast<char*>(curr) <= reinterpret_cast<char*>(_occupied_ptr)) {
            curr = get_next_occupied(curr);
        }
        if (curr) {
            _occupied_ptr = curr;
            _occupied = true;
        } else {
            _occupied_ptr = pool_end;
            _occupied = false;
        }
    }
    return *this;
}
// проходим по всему с конца
allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (!_trusted_memory) return *this;
    char* pool_start = reinterpret_cast<char*>(_trusted_memory) + sizeof(allocator_meta);
    if (_occupied_ptr == pool_start) return *this;

    if (!_occupied) {
        void* curr = get_first_occupied(_trusted_memory);
        void* last_occ = nullptr;
        while (curr && reinterpret_cast<char*>(curr) < reinterpret_cast<char*>(_occupied_ptr)) {
            last_occ = curr;
            curr = get_next_occupied(curr);
        }

        if (last_occ) {
            char* end_of_last = reinterpret_cast<char*>(last_occ) + occupied_block_metadata_size + get_block_size(last_occ);
            if (end_of_last < reinterpret_cast<char*>(_occupied_ptr)) {
                 _occupied_ptr = last_occ;
                 _occupied = true;
            } else {
                 _occupied_ptr = last_occ;
                 _occupied = true;
            }
        } else {
            _occupied_ptr = pool_start;
            _occupied = false;
        }
    } else {
        void* prev_occ = get_prev_occupied(_occupied_ptr);
        char* left_limit = prev_occ ? reinterpret_cast<char*>(prev_occ) + occupied_block_metadata_size + get_block_size(prev_occ) : pool_start;

        if (left_limit < reinterpret_cast<char*>(_occupied_ptr)) {
            _occupied_ptr = left_limit;
            _occupied = false;
        } else {
            _occupied_ptr = prev_occ;
            _occupied = true;
        }
    }
    return *this;
}
// создаем копию состояния и сдвигаем дальше
allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    boundary_iterator temp = *this;
    ++(*this);
    return temp;
}
// создаем копию состояния и сдвигаем назад
allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    boundary_iterator temp = *this;
    --(*this);
    return temp;
}

// узнать размер блока, на который сейчас смотрим
size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    if (!_trusted_memory || !_occupied_ptr) return 0;
    char* pool_end = reinterpret_cast<char*>(_trusted_memory) + get_space_size(_trusted_memory);
    if (_occupied_ptr == pool_end) return 0;

    if (_occupied) {
        return get_block_size(_occupied_ptr);
    } else {
        void* curr = get_first_occupied(_trusted_memory);
        while (curr && reinterpret_cast<char*>(curr) <= reinterpret_cast<char*>(_occupied_ptr)) {
            curr = get_next_occupied(curr);
        }
        char* end_of_gap = curr ? reinterpret_cast<char*>(curr) : pool_end;
        return end_of_gap - reinterpret_cast<char*>(_occupied_ptr);
    }
}

// узнать, занят ли сейчас этот блок
bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied;
}

// получить адрес самих ДАННЫХ внутри блока
void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    if (!_occupied_ptr) return nullptr;
    if (_occupied) {
        return reinterpret_cast<char*>(_occupied_ptr) + occupied_block_metadata_size;
    }
    return _occupied_ptr;
}

// пустой конструктор итератора
allocator_boundary_tags::boundary_iterator::boundary_iterator()
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr)
{
}

// определяем с занятого либо со свободного начинается память
allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted)
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(trusted)
{
    if (trusted != nullptr) {
        char* pool_start = reinterpret_cast<char*>(trusted) + sizeof(allocator_meta);
        void* first_occ = get_first_occupied(trusted);
        if (first_occ && reinterpret_cast<char*>(first_occ) == pool_start) {
            _occupied_ptr = first_occ;
            _occupied = true;
        } else {
            _occupied_ptr = pool_start;
            _occupied = false;
        }
    }
}

// просто вернуть сырой адрес текущего блок
void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}