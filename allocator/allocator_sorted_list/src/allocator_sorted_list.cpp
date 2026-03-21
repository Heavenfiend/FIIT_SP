#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"
#include <stdexcept>
#include <cstring>

namespace {


    // Кто хозяин
    std::pmr::memory_resource*& get_parent(void* trusted) {
        return *reinterpret_cast<std::pmr::memory_resource**>(trusted);
    }

    // режим выбора блока
    allocator_with_fit_mode::fit_mode& get_mode(void* trusted) {
        return *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*)
        );
    }

    // общий размер памяти
    size_t& get_size(void* trusted) {
        return *reinterpret_cast<size_t*>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode)
        );
    }

    // защита от потоков
    std::mutex& get_mutex(void* trusted) {
        return *reinterpret_cast<std::mutex*>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t)
        );
    }

    // первый свободный блок
    void*& get_first_free(void* trusted) {
        return *reinterpret_cast<void**>(
            reinterpret_cast<uint8_t*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t) + sizeof(std::mutex)
        );
    }

    // шапка конкретного блока

    // размер куска
    size_t& get_block_size(void* block) {
        return *reinterpret_cast<size_t*>(block);
    }

    // следующий свободный
    void*& get_next_free(void* block) {
        return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(block) + sizeof(size_t));
    }

    // адрес данных
    void* get_block_payload(void* block) {
        return reinterpret_cast<uint8_t*>(block) + sizeof(size_t) + sizeof(void*);
    }

    // найти заголовок
    void* get_block_from_payload(void* payload) {
        return reinterpret_cast<uint8_t*>(payload) - sizeof(size_t) - sizeof(void*);
    }

    // проверка занятости
    bool is_block_occupied(void* block) {
        return get_next_free(block) == block;
    }

    // пометить занятым
    void set_block_occupied(void* block) {
        get_next_free(block) = block;
    }
}

allocator_sorted_list::~allocator_sorted_list()
{
    if (!_trusted_memory) return;

    std::pmr::memory_resource* parent = get_parent(_trusted_memory);
    size_t size = get_size(_trusted_memory);
    // вручную вызываем деструктор мьютекса
    get_mutex(_trusted_memory).~mutex();
    // отдаем память родителю
    parent->deallocate(_trusted_memory, size);
}
 // забираем ресурсы у одного и передаем другому
allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}
// проверяем на самоприсваивание, чистим свою память и забираем чужую
allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
    if (this == &other) return *this;
    this->~allocator_sorted_list();
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
    return *this;
}
// запрашиваем память у системы и размечаем её: создаем шапку и первый огромный свободный блок
allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    // если родитель не указан, берем память черзез new
    if (!parent_allocator) {
        parent_allocator = std::pmr::get_default_resource();
    }
    // проверяем, влезет ли в этот размер хотя бы одна минимальная структура
    if (space_size < allocator_metadata_size + block_metadata_size) {
        throw std::bad_alloc();
    }
    // просим у системы один большой кусок памяти
    _trusted_memory = parent_allocator->allocate(space_size);

    std::memset(_trusted_memory, 0, space_size);
    // записываем в начало памяти наши метаданные аллокатора
    get_parent(_trusted_memory) = parent_allocator;
    get_mode(_trusted_memory) = allocate_fit_mode;
    get_size(_trusted_memory) = space_size;

    new (&get_mutex(_trusted_memory)) std::mutex();
    // вычисляем, где начнется наш первый блок
    void* first_block = reinterpret_cast<uint8_t*>(_trusted_memory) + allocator_metadata_size;
    get_first_free(_trusted_memory) = first_block;

    get_block_size(first_block) = space_size - allocator_metadata_size - block_metadata_size;

    get_next_free(first_block) = nullptr;
}

// ищем подходящий свободный блок, отрезаем от него нужный кусок и перестраиваем список
[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    size_t size)
{
    // если памяти вообще нет — кидаем ошибку
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    // вешаем мьютесыв
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    void* prev_block = nullptr;
    void* current_block = get_first_free(_trusted_memory);

    void* best_prev = nullptr;
    void* best_block = nullptr;

    // смотрим, какой у нас режим поиска
    allocator_with_fit_mode::fit_mode mode = get_mode(_trusted_memory);
    // бежим по цепочке свободных блоков
    while (current_block) {
        size_t block_size = get_block_size(current_block);
        // если блок достаточно большой для нашего запроса
        if (block_size >= size) {
            if (mode == allocator_with_fit_mode::fit_mode::first_fit) {
                // берем самый первый подошедший и выходим из цикла #1 случай
                best_prev = prev_block;
                best_block = current_block;
                break;
            } else if (mode == allocator_with_fit_mode::fit_mode::the_best_fit) {
                // ищем такой, который максимально близок к нужному размеру #2 случай
                if (!best_block || block_size < get_block_size(best_block)) {
                    best_prev = prev_block;
                    best_block = current_block;
                }
            } else if (mode == allocator_with_fit_mode::fit_mode::the_worst_fit) {
                // ищем самый огромный блок #3 случай
                if (!best_block || block_size > get_block_size(best_block)) {
                    best_prev = prev_block;
                    best_block = current_block;
                }
            }
        }
        // переходим к следующему звену цепи
        prev_block = current_block;
        current_block = get_next_free(current_block);
    }
    // если ничего не нашли — памяти не хватило
    if (!best_block) {
        throw std::bad_alloc();
    }

    size_t best_size = get_block_size(best_block);
    // решаем: дробить блок на два или отдать целиком
    if (best_size >= size + block_metadata_size + 1) {
        // вычисляем адрес, где начнется наш остаток
        void* new_free_block = reinterpret_cast<uint8_t*>(best_block) + block_metadata_size + size;
        // записываем в остаток его новый уменьшившийся размер
        get_block_size(new_free_block) = best_size - block_metadata_size - size;
        get_next_free(new_free_block) = get_next_free(best_block);
        // текущий блок теперь ровно того размера, который просили
        get_block_size(best_block) = size;
        set_block_occupied(best_block);
        // вставляем  остаток в цепочку свободных блоков вместо старого большого блока
        if (best_prev) {
            get_next_free(best_prev) = new_free_block;
        } else {
            get_first_free(_trusted_memory) = new_free_block;
        }
    } else {
        // если блок почти совпадает по размеру дробить нет смысла,
        // просто выкидываем его из списка свободных
        if (best_prev) {
            get_next_free(best_prev) = get_next_free(best_block);
        } else {
            get_first_free(_trusted_memory) = get_next_free(best_block);
        }
    }
    // помечаем, что наш выбранный блок теперь занят
    set_block_occupied(best_block);

    return get_block_payload(best_block);
}
// создаем полную копию аллокатора: копируем память и пересчитываем все указатели внутри
allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
{
    // если копировать нечего — зануляемся и выходим
    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return;
    }

    std::pmr::memory_resource* parent = get_parent(other._trusted_memory);
    size_t size = get_size(other._trusted_memory);

    // выделяем себе такой же кусок памяти сколько и у родителя
    _trusted_memory = parent->allocate(size);
    std::memcpy(_trusted_memory, other._trusted_memory, size);
    // создаем новый мьютекс
    new (&get_mutex(_trusted_memory)) std::mutex();

    void* other_first = get_first_free(other._trusted_memory);
    if (other_first) {
        // вычисляем где новый первый свободный блок памяти у нас
        get_first_free(_trusted_memory) = reinterpret_cast<uint8_t*>(_trusted_memory) +
            (reinterpret_cast<uint8_t*>(other_first) - reinterpret_cast<uint8_t*>(other._trusted_memory));

        void* current_other = other_first;
        void* current_this = get_first_free(_trusted_memory);

        while (get_next_free(current_other)) {
            void* next_other = get_next_free(current_other);
            // считаем адрес следующего блока относительно нашей новой памяти
            void* next_this = reinterpret_cast<uint8_t*>(_trusted_memory) +
                (reinterpret_cast<uint8_t*>(next_other) - reinterpret_cast<uint8_t*>(other._trusted_memory));

            get_next_free(current_this) = next_this;

            current_other = next_other;
            current_this = next_this;
        }
    }
}

// сначала полностью очищаем себя, а потом создаем внутри точную копию другого аллокатора
allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    if (this == &other) return *this;

    this->~allocator_sorted_list();

    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return *this;
    }
    // берем данные о размере и родителе у оригинала
    std::pmr::memory_resource* parent = get_parent(other._trusted_memory);
    size_t size = get_size(other._trusted_memory);
    // выделяем новый чистый кусок памяти такого же размера
    _trusted_memory = parent->allocate(size);
    std::memcpy(_trusted_memory, other._trusted_memory, size);
    // создаем новый мьютекс на новом месте
    new (&get_mutex(_trusted_memory)) std::mutex();

    void* other_first = get_first_free(other._trusted_memory);
    if (other_first) {
        // вычисляем адрес первого свободного блока в нашей новой памяти
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
    // возвращаем ссылку на обновленного себя
    return *this;
}
// проверяем, являются ли два аллокатора по сути одним и тем же объектом
bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    // пытаемся привести чужой указатель к нашему типу
    auto* other_allocator = dynamic_cast<const allocator_sorted_list*>(&other);
    if (!other_allocator) return false;
    return _trusted_memory == other_allocator->_trusted_memory;
}
// возвращаем память в список свободных и склеиваем соседние пустые блоки в один большой
void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (!_trusted_memory) return;

    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    // прыгаем назад от данных пользователя к заголовку блока
    void* block = get_block_from_payload(at);
    size_t size = get_size(_trusted_memory);

    // проверяем адрес из нашей ли памяти
    if (at < reinterpret_cast<uint8_t*>(_trusted_memory) + allocator_metadata_size + block_metadata_size ||
        at >= reinterpret_cast<uint8_t*>(_trusted_memory) + size) {
        throw std::logic_error("Pointer does not belong to this allocator");
    }
    // ищем место в списке свободных блоков, куда вставить наш освобождаемый блок
    void* prev_free = nullptr;
    void* current_free = get_first_free(_trusted_memory);

    while (current_free && current_free < block) {
        prev_free = current_free;
        current_free = get_next_free(current_free);
    }
    // втыкаем наш блок в цепочку
    get_next_free(block) = current_free;

    if (prev_free) {
        get_next_free(prev_free) = block; // левый сосед теперь указывает на нас
    } else {
        get_first_free(_trusted_memory) = block; // мы стали самым первым свободным блоком
    }
    // Пробуем склеить ПРАВ соседом
    // если конец нашего блока — это в точности начало следующего свободного блока
    if (current_free && reinterpret_cast<uint8_t*>(block) + block_metadata_size + get_block_size(block) == reinterpret_cast<uint8_t*>(current_free)) {
        get_block_size(block) += block_metadata_size + get_block_size(current_free);
        get_next_free(block) = get_next_free(current_free);
    }
    // Пробуем склеить с ЛЕВЫМ соседом
    // если конец левого соседа — это начало нашего блока
    if (prev_free && reinterpret_cast<uint8_t*>(prev_free) + block_metadata_size + get_block_size(prev_free) == reinterpret_cast<uint8_t*>(block)) {
        get_block_size(prev_free) += block_metadata_size + get_block_size(block);
        get_next_free(prev_free) = get_next_free(block);

        // затираем свои старые метаданные для чистоты
        std::memset(block, 0, block_metadata_size);
    }
}
// просто меняем правило, по которому аллокатор выбирает свободный блок
inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    if (!_trusted_memory) return;
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    get_mode(_trusted_memory) = mode;
}
// собираем информацию о всех блоках
std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    if (!_trusted_memory) return {};
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    return get_blocks_info_inner();
}

// пробегаемся по всей памяти и записываем размер и статус каждого блока в один список
std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> info;
    for (auto it = begin(); it != end(); ++it) {
        info.push_back({it.size(), it.occupied()});
    }
    return info;
}
// создаем бегунок, который встает на самую первую свободную дырку
allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    if (!_trusted_memory) return sorted_free_iterator();
    return sorted_free_iterator(get_first_free(_trusted_memory));
}
// создаем пустой бегунок, который означает, что свободные блоки закончились
allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator(nullptr);
}
// встаем в самое начало памяти (сразу после шапки аллокатора)
allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    if (!_trusted_memory) return sorted_iterator();
    return sorted_iterator(_trusted_memory);
}
// создаем маркер для полного обхода памяти
allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return sorted_iterator();
}

// проверяем, стоят ли два бегунка на одном и том же свободном блоке
bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}
// проверяем, что бегунки указывают на разные свободные блоки
bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return _free_ptr != other._free_ptr;
}
// прыгаем к следующей свободной дырке по указателю из заголовка текущего блока
allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr) {
        _free_ptr = get_next_free(_free_ptr);
    }
    return *this;
}
// делаем шаг вперед
allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    sorted_free_iterator tmp = *this;
    ++(*this);
    return tmp;
}
// узнаем размер свободной дырки, на которой сейчас стоим
size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    if (!_free_ptr) return 0;
    return get_block_size(_free_ptr);
}
// получаем прямой адрес блока, на который смотрит бегунок
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
// сравниваем позиции двух бегунков при полном обходе памяти
bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    return _current_ptr == other._current_ptr;
}
// проверяем, не догнали ли бегунки друг друга
bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return _current_ptr != other._current_ptr;
}
// шагаем по памяти, перепрыгивая через полезные данные в начало следующего блока
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
// запоминаем где стояли, делаем шаг, возвращаем старую позицию
allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    sorted_iterator tmp = *this;
    ++(*this);
    return tmp;
}
// просто узнаем размер блока, на котором сейчас стоит наш бегунок
size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    if (!_current_ptr) return 0;
    return get_block_size(_current_ptr);
}
// получаем прямой адрес блока
void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    return _current_ptr;
}
// ставим бегунок на самый первый блок (сразу за большой шапкой аллокатора)
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
// проверяем: занят ли текущий бло
bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    if (!_current_ptr) return false;

    void* current_free = get_first_free(_trusted_memory);
    while (current_free && current_free < _current_ptr) {
        current_free = get_next_free(current_free);
    }

    return current_free != _current_ptr;
}
