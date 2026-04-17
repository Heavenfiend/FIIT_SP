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
inline void*& get_first_block(void* trusted_memory) {
    return reinterpret_cast<allocator_meta*>(trusted_memory)->first_block;
}
// читаем размер текущего блока
inline size_t& get_block_size(void* block_ptr) {
    return *reinterpret_cast<size_t*>(block_ptr);
}
// узнаем статус: если тут nullptr — блок свободен, иначе — занят
inline void*& get_trusted_memory_ptr(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t)
    );
}
// получаем адрес соседа справа отступ 16 байт
inline void*& get_next_block_ptr(void* block_ptr) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(block_ptr) + sizeof(size_t) + sizeof(void*)
    );
}
// получаем адрес соседа слева отступ 24 байт
inline void*& get_prev_block_ptr(void* block_ptr) {
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


// конструктор: запрашиваем большой кусок и готовим его к работе
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
    // заполняем хуйню метаданными
    get_parent_allocator(_trusted_memory) = parent_allocator;
    get_fit_mode(_trusted_memory) = allocate_fit_mode;
    get_space_size(_trusted_memory) = total_size;
    // создаем мьютекс
    new (&get_mutex(_trusted_memory)) std::mutex();
    // первый блок за шапкой сразуууууу
    void* first_block = reinterpret_cast<char*>(_trusted_memory) + sizeof(allocator_meta);
    get_first_block(_trusted_memory) = first_block;
    // инициализируем как один свободный блок и вычитаем из размера блока шапку чтобы получить полезный размер
    if (space_size >= occupied_block_metadata_size) {
        get_block_size(first_block) = space_size - occupied_block_metadata_size;
        // nullptr значит свободный
        get_trusted_memory_ptr(first_block) = nullptr;
    } else {
        // мало памсяти
        get_block_size(first_block) = 0;
        get_trusted_memory_ptr(first_block) = _trusted_memory; // занятый или недоступный
    }
    get_next_block_ptr(first_block) = nullptr;
    get_prev_block_ptr(first_block) = nullptr;
}
// здесь мы ищем подходящий свободный кусок и, если он слишком большой, отрезаем от него нужную часть
[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    // мутекс
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));

    auto fit_mode = get_fit_mode(_trusted_memory);
    void* current_block = get_first_block(_trusted_memory);
    void* target_block = nullptr;

    size_t needed_size = size;
    // идем по всем блокам и занятым и не
    while (current_block != nullptr) {
        // nullptr -> свободная хуйня
        if (get_trusted_memory_ptr(current_block) == nullptr) {
            size_t current_size = get_block_size(current_block);
            // проверяем на то влезет ли данные в блок свободный
            if (current_size >= needed_size) {
                // выбираем по стратегиям (first, best или worst fit)
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
        // прыгаем к следующему соседу в двусвязном списке
        current_block = get_next_block_ptr(current_block);
    }
    // если прошли всю память и ничего не нашли — кидаем ошибку
    if (target_block == nullptr) {
        throw std::bad_alloc();
    }

    size_t target_size = get_block_size(target_block);
    // если блок намного больше то дробим его
    if (target_size >= needed_size + occupied_block_metadata_size) {
        // вычисляем хвост
        void* new_block = reinterpret_cast<char*>(target_block) + occupied_block_metadata_size + needed_size;

        // настраиваем шапку нового свободного блока
        get_block_size(new_block) = target_size - needed_size - occupied_block_metadata_size;
        get_trusted_memory_ptr(new_block) = nullptr;
        get_next_block_ptr(new_block) = get_next_block_ptr(target_block);
        get_prev_block_ptr(new_block) = target_block;
        // вклинваем между текущим и следующим
        if (get_next_block_ptr(target_block) != nullptr) {
            get_prev_block_ptr(get_next_block_ptr(target_block)) = new_block;
        }
        get_next_block_ptr(target_block) = new_block;
        get_block_size(target_block) = needed_size;
    } else {

        needed_size = target_size;
    }
    // помечаем блок как занятый
    get_trusted_memory_ptr(target_block) = _trusted_memory;
    // возвращаем пользователю адрес СРАЗУ послу шапки блока
    return reinterpret_cast<char*>(target_block) + occupied_block_metadata_size;
}

// освобождение памяти: мгновенная склейка соседей
void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    // если прислали пустой указатель
    if (at == nullptr) return;
    // блокируем мьютекс
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    // попадаем в шапку блока
    void* target_block = reinterpret_cast<char*>(at) - occupied_block_metadata_size;
    // проверяем на принадлежность этому блоку
    if (get_trusted_memory_ptr(target_block) != _trusted_memory) {
        throw std::logic_error("Attempted to deallocate memory not owned by this allocator");
    }
    // помечаем как свободный
    get_trusted_memory_ptr(target_block) = nullptr;
    // склейка с правым соседом : проверяем что существует потом просто добавляем память а соседа выкидываем
    void* next_block = get_next_block_ptr(target_block);
    if (next_block != nullptr && get_trusted_memory_ptr(next_block) == nullptr) {
        get_block_size(target_block) += get_block_size(next_block) + occupied_block_metadata_size;
        get_next_block_ptr(target_block) = get_next_block_ptr(next_block);
        if (get_next_block_ptr(next_block) != nullptr) {
            get_prev_block_ptr(get_next_block_ptr(next_block)) = target_block;
        }
    }
    // склейка с левым, только теперь он нас с собой склеивает и его правый сосед терь тот кто справа от нас
    void* prev_block = get_prev_block_ptr(target_block);
    if (prev_block != nullptr && get_trusted_memory_ptr(prev_block) == nullptr) {
        get_block_size(prev_block) += get_block_size(target_block) + occupied_block_metadata_size;
        get_next_block_ptr(prev_block) = get_next_block_ptr(target_block);
        if (get_next_block_ptr(target_block) != nullptr) {
            get_prev_block_ptr(get_next_block_ptr(target_block)) = prev_block;
        }
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
    return boundary_iterator(nullptr);
}
// функция сборки статистики: идет по всем блокам подряд и записывает в массив их размер и статус (занят/свободен)
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

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto* other_alloc = dynamic_cast<const allocator_boundary_tags*>(&other);
    return other_alloc != nullptr && _trusted_memory == other_alloc->_trusted_memory;
}
// сравнение итераторов: указывают ли они на один и тот же блок
bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr;
}
// просто читаем адрес левого соседа
bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return _occupied_ptr != other._occupied_ptr;
}
// постфиксные шаги
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
// узнать размер блока, на который сейчас смотрим
size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    if (!_occupied_ptr) return 0;
    return get_block_size(_occupied_ptr);
}
// узнать, занят ли сейчас этот блок
bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    if (!_occupied_ptr) return false;
    return get_trusted_memory_ptr(_occupied_ptr) != nullptr;
}
// получить адрес самих ДАННЫХ внутри блока
void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    if (!_occupied_ptr) return nullptr;
    return reinterpret_cast<char*>(_occupied_ptr) + occupied_block_metadata_size;
}
// пустой конструктор итератора
allocator_boundary_tags::boundary_iterator::boundary_iterator()
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr)
{
}
// ставим указку на самый первый блок
allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted)
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(trusted)
{
    if (trusted != nullptr) {
        _occupied_ptr = get_first_block(trusted);
    }
}
// просто вернуть сырой адрес текущего блок
void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}
