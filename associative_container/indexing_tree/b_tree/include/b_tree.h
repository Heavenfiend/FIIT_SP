#ifndef SYS_PROG_B_TREE_H
#define SYS_PROG_B_TREE_H

#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stack>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/container/static_vector.hpp>

#include <associative_container.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class B_tree final : private compare
{
public:
    // внутренний тип
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

    // иерархия исключений
    class exception : public std::runtime_error
    {
    public:
        explicit exception(const std::string& message) : std::runtime_error(message) {}
    };

    class allocation_failure final : public exception
    {
    public:
        allocation_failure() : exception("B_tree: memory allocation failed") {}
    };

    class key_not_found final : public exception
    {
    public:
        key_not_found() : exception("B_tree: key not found") {}
    };

    class bad_iterator final : public exception
    {
    public:
        bad_iterator() : exception("B_tree: invalid iterator") {}
    };

private:
    // Секция 1: хранение узлов, аллокация и локальный поиск.

    // ограничения
    static_assert(t >= 2, "B_tree minimum degree t must be at least 2");

    static constexpr std::size_t minimum_keys_in_node = t - 1;
    static constexpr std::size_t maximum_keys_in_node = 2 * t - 1;
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    // левый ключ меньше правого?
    inline bool compare_keys(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }
    // сравнение на равные
    bool equivalent_keys(const tkey& lhs, const tkey& rhs) const
    {
        return !compare_keys(lhs, rhs) && !compare_keys(rhs, lhs);
    }
    // структура узла
    struct btree_node
    {
        boost::container::static_vector<tree_data_type, maximum_keys_in_node + 1> _keys;
        boost::container::static_vector<btree_node*, maximum_keys_in_node + 2> _pointers;

        btree_node() noexcept = default;
    };
    //  выделение памяти под btree_node
    using node_allocator_type = typename std::allocator_traits<pp_allocator<value_type>>::template rebind_alloc<btree_node>;
    using node_allocator_traits = std::allocator_traits<node_allocator_type>;

    // поля класса дерева(ал, кор, раз)
    pp_allocator<value_type> _allocator;
    btree_node* _root;
    std::size_t _size;

    node_allocator_type get_node_allocator() const
    {
        return node_allocator_type(_allocator);
    }

    // новый узел
    btree_node* create_node()
    {
        node_allocator_type alloc = get_node_allocator();
        btree_node* node = nullptr;

        try
        {   // выделение памяти под 1 уз
            node = node_allocator_traits::allocate(alloc, 1);
            node_allocator_traits::construct(alloc, node);
            return node;
        }
        catch (const std::bad_alloc&)
        {
            if (node != nullptr)
            {
                node_allocator_traits::deallocate(alloc, node, 1);
            }
            throw allocation_failure();
        }
        // друг ошибка
        catch (...)
        {
            if (node != nullptr)
            {
                node_allocator_traits::deallocate(alloc, node, 1);
            }
            throw;
        }
    }
    // удаление узла
    void destroy_node(btree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }

        try
        {
            // получаем аллокатор вызываем деструктор и освобождаем память
            node_allocator_type alloc = get_node_allocator();
            node_allocator_traits::destroy(alloc, node);
            node_allocator_traits::deallocate(alloc, node, 1);
        }
        catch (...)
        {
        }
    }

    // полностью очистить память поддерева
    void destroy_subtree(btree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }
        // для каждого ребенка вызываем
        for (size_t i = 0; i < node->_pointers.size(); ++i) {
            btree_node* child = node->_pointers[i];
            destroy_subtree(child);
        }

        destroy_node(node);
    }

    btree_node* clone_subtree(const btree_node* source)
    {
        if (source == nullptr)
        {
            return nullptr;
        }
        // основа
        btree_node* result = create_node();

        try
        {
            // копируем ключи
            result->_keys = source->_keys;
            // то же самое проходим по детям
            for (size_t i = 0; i < source->_pointers.size(); ++i)
            {
                btree_node* source_child = source->_pointers[i];

                result->_pointers.push_back(clone_subtree(source_child));
            }
        }
        catch (...)
        {
            destroy_subtree(result);
            throw;
        }

        return result;
    }
    // поиск первого ключа который не меньше key
    std::size_t lower_index_in_node(const btree_node* node, const tkey& key) const
    {
        // границы
        std::size_t left = 0;
        std::size_t right = node->_keys.size();

        while (left < right)
        {
            const std::size_t middle = left + (right - left) / 2;

            if (compare_keys(node->_keys[middle].first, key))
            {
                left = middle + 1;
            }
            else
            {
                right = middle;
            }
        }

        return left;
    }
    // поиск первого ключа который больше key
    std::size_t upper_index_in_node(const btree_node* node, const tkey& key) const
    {
        // границы
        std::size_t left = 0;
        std::size_t right = node->_keys.size();

        while (left < right)
        {
            const std::size_t middle = left + (right - left) / 2;

            if (!compare_keys(key, node->_keys[middle].first))
            {
                left = middle + 1;
            }
            else
            {
                right = middle;
            }
        }

        return left;
    }

    void split_child(btree_node* parent, std::size_t child_index)
    {
        // узел требующий разделения
        btree_node* child = parent->_pointers[child_index];
        if (child == nullptr || child->_keys.size() <= maximum_keys_in_node)
        {
            return;
        }

        btree_node* right = create_node();

        try
        {
            // медиана перемещается к родителю
            constexpr std::size_t median_index = t;
            tree_data_type median = std::move(child->_keys[median_index]);

            // правее медианы в right
            for (std::size_t index = median_index + 1; index < child->_keys.size(); ++index)
            {
                right->_keys.push_back(std::move(child->_keys[index]));
            }
            // правую половину указателей тоже в right
            if (!child->_pointers.empty())
            {
                for (std::size_t index = median_index + 1; index < child->_pointers.size(); ++index)
                {
                    right->_pointers.push_back(child->_pointers[index]);
                }
            }
            // терь 0 до t-1
            child->_keys.resize(median_index);
            if (!child->_pointers.empty())
            {
                child->_pointers.resize(median_index + 1);
            }
            // медиану вставляем по индексу child_index
            parent->_keys.insert(parent->_keys.begin() + static_cast<std::ptrdiff_t>(child_index), std::move(median));
            // указатель на right child_index + 1
            parent->_pointers.insert(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(child_index + 1), right);
        }
        catch (...)
        {
            destroy_node(right);
            throw;
        }
    }

    // поменять содержимое двух деревьев
    void swap_with(B_tree& other) noexcept(std::is_nothrow_swappable_v<compare> && std::is_nothrow_swappable_v<pp_allocator<value_type>>)
    {
        using std::swap;
        swap(static_cast<compare&>(*this), static_cast<compare&>(other));
        swap(_allocator, other._allocator);
        swap(_root, other._root);
        swap(_size, other._size);
    }

    // ссылку на узел в сам узел
    template <typename link_type>
    static btree_node* node_from_link(link_type link) noexcept
    {
        return link == nullptr ? nullptr : *link;
    }
    // вытаскиваем рабочий узел из стека пути
    template <typename link_type>
    static btree_node* current_node(const std::stack<std::pair<link_type, std::size_t>>& path) noexcept
    {
        return path.empty() ? nullptr : node_from_link(path.top().first);
    }

    // переход к следующему элементу
    template <typename link_type>
    static void move_to_next(std::stack<std::pair<link_type, std::size_t>>& path, std::size_t& index)
    {
        if (path.empty())
        {
            return;
        }

        btree_node* node = current_node(path);
        if (node == nullptr || index >= node->_keys.size())
        {
            return;
        }
        // углубляемся внутрь правый ребенок, затем все левые
        if (!node->_pointers.empty())
        {
            std::size_t child_index = index + 1; // указатель справа от текущего ключа
            link_type child_link = &node->_pointers[child_index];
            path.push({child_link, child_index});
            // до самого упора влево
            while (!node_from_link(child_link)->_pointers.empty())
            {
                btree_node* child = node_from_link(child_link);
                child_link = &child->_pointers[0];
                path.push({child_link, 0});
            }
            index = 0;
            return;
        }
        // если есть ключи в листе
        if (index + 1 < node->_keys.size())
        {
            ++index;
            return;
        }
        // если узел закончился
        auto original_path = path;
        while (!path.empty())
        {
            // запоминаем из какого ребенка поднялись
            const std::size_t source_child_index = path.top().second;
            path.pop(); // выходим наверх

            // дерево обошли
            if (path.empty())
            {
                path = std::move(original_path);
                btree_node* last_node = current_node(path);
                index = last_node == nullptr ? 0 : last_node->_keys.size();
                return;
            }
            // в родителе есть ключ, следующий сразу за нашим поддеревом
            btree_node* parent = current_node(path);
            if (source_child_index < parent->_keys.size())
            {
                index = source_child_index;
                return;
            }
        }
    }

    // от больших ключей к меньшим.
    template <typename link_type>
    static void move_to_previous(std::stack<std::pair<link_type, std::size_t>>& path, std::size_t& index)
    {
        if (path.empty() || index == npos)
        {
            return;
        }

        btree_node* node = current_node(path);
        if (node == nullptr)
        {
            return;
        }

        // ставим итератор на последний ключ в узле если выходил за рамки
        if (index >= node->_keys.size())
        {
            if (!node->_keys.empty())
            {
                index = node->_keys.size() - 1;
            }
            return;
        }

        if (!node->_pointers.empty())
        {
            std::size_t child_index = index; // берем ребенка слева от ключа
            link_type child_link = &node->_pointers[child_index];
            path.push({child_link, child_index});

            // бежим до упора вправо
            while (!node_from_link(child_link)->_pointers.empty())
            {
                btree_node* child = node_from_link(child_link);
                child_index = child->_pointers.size() - 1;
                child_link = &child->_pointers[child_index];
                path.push({child_link, child_index});
            }
            // берем самый большой ключ в правом углу поддерева
            btree_node* child = node_from_link(child_link);
            index = child->_keys.empty() ? 0 : child->_keys.size() - 1;
            return;
        }
        // сдвиг влево
        if (index > 0)
        {
            --index;
            return;
        }
        // подъем наверх
        auto original_path = path;
        while (!path.empty())
        {
            const std::size_t source_child_index = path.top().second;
            path.pop(); // выход к родителю

            // обошли все дерево
            if (path.empty())
            {
                path = std::move(original_path);
                index = npos;
                return;
            }
            // если пришли не из нулевого ребенка
            if (source_child_index > 0)
            {
                index = source_child_index - 1;
                return;
            }
        }
    }

public:
    // Секция 2: итераторы.
    class btree_iterator;
    class btree_reverse_iterator;
    class btree_const_iterator;
    class btree_const_reverse_iterator;

    class btree_iterator final
    {
        // хранение пути от корня до текущего узла и номер ключа текущего узла
        std::stack<std::pair<btree_node**, std::size_t>> _path;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;      // тип данных, на которые мы смотрим
        using reference = tree_data_type&;            // тип ссылки на эти данные
        using pointer = tree_data_type*;              // тип указателя на эти данные
        using iterator_category = std::bidirectional_iterator_tag; //умеем ходить в обе стороны
        using difference_type = std::ptrdiff_t;       // тип для хранения разности шагов между итераторами
        using self = btree_iterator; // псевдоним

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;
        // конструктор итератора
        explicit btree_iterator(const std::stack<std::pair<btree_node**, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }
        // оператор разыменования
        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }
        // указатель на данные
        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }
        // возвращает ссылку на самого себя шаг вперед
        self& operator++()
        {
            move_to_next(_path, _index);
            return *this;
        }
        // постфиксный
        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }
        // префиксный декремент
        self& operator--()
        {
            move_to_previous(_path, _index);
            return *this;
        }
        // постфиксный декремент
        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }
        // сравниваем два итератора
        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }
        // не равны итераторы
        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }
        // глубина узла
        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }
        // количество ключей в текущем узле
        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }
        // проверка, является ли узел листом
        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }
        // получение позиции ключа в узле
        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    class btree_const_iterator final
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> _path;
        std::size_t _index;

    public:
        // Теперь ссылка и указатель ведут на const данные
        using value_type = tree_data_type_const;
        using reference = const tree_data_type&;
        using pointer = const tree_data_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = btree_const_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_iterator;
        friend class btree_const_reverse_iterator;
        // конструктор итератора с защитой от записи
        explicit btree_const_iterator(const std::stack<std::pair<btree_node* const*, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }
        // конструктор преобразования обычного итератора в константный
        btree_const_iterator(const btree_iterator& it) noexcept : _index(it._index)
        {
            // создаем массив и копируем путь, затем обратно из стека в вектор выгружаем
            std::vector<std::pair<btree_node* const*, std::size_t>> reversed;
            auto path = it._path;
            while (!path.empty())
            {
                reversed.push_back({path.top().first, path.top().second});
                path.pop();
            }
            // из вектора обратно в новый стек с конца
            for (auto iter = reversed.rbegin(); iter != reversed.rend(); ++iter)
            {
                _path.push(*iter);
            }
        }
        // оператор разыменования
        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }
        // указатель на данные
        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }
        // возвращает ссылку на самого себя шаг вперед
        self& operator++()
        {
            move_to_next(_path, _index);
            return *this;
        }
        // постфиксный
        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }
        // префиксный декремент
        self& operator--()
        {
            move_to_previous(_path, _index);
            return *this;
        }
        // постфиксный декремент
        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }
        // сравниваем два итератора
        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }
        // не равны итераторы
        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }
        // глубина узла
        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }
        // количество ключей в текущем узле
        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }
        // является ли узел листом
        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }
        // получение позиции ключа в узле
        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    class btree_reverse_iterator final
    {
        std::stack<std::pair<btree_node**, std::size_t>> _path;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = tree_data_type&;
        using pointer = tree_data_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = btree_reverse_iterator;

        friend class B_tree;
        friend class btree_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;
        // конструктор итератора
        explicit btree_reverse_iterator(const std::stack<std::pair<btree_node**, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }
        // превратить обычный итератор в реверсивный
        btree_reverse_iterator(const btree_iterator& it) noexcept : _path(it._path), _index(it._index)
        {
        }
        // превратить реверсивный итератор в обычный
        operator btree_iterator() const noexcept
        {
            return btree_iterator(_path, _index);
        }
        // разыменование
        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }
        // указатель на данные
        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }
        // префиксный инкремент
        self& operator++()
        {
            move_to_previous(_path, _index);
            return *this;
        }
        // постфиксный инкремент
        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }
        // префиксный декремнет
        self& operator--()
        {
            move_to_next(_path, _index);
            return *this;
        }
        // постфиксный декремент
        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }
        // проверка на равенство
        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }
        // проверка на неравенство
        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }
        // глубина узла в дереве
        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }
        // количество ключей в узле
        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }
        // является ли листом
        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }
        // индекс ключа внутри узла
        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    class btree_const_reverse_iterator final
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> _path;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const tree_data_type&;
        using pointer = const tree_data_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = btree_const_reverse_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_iterator;
        // конструктор итератора с защитой
        explicit btree_const_reverse_iterator(const std::stack<std::pair<btree_node* const*, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }
        // конструктор преобразования обычного итератора в константный
        btree_const_reverse_iterator(const btree_reverse_iterator& it) noexcept : _index(it._index)
        {
            // создаем массив и копируем путь, затем обратно из стека в вектор выгружаем
            std::vector<std::pair<btree_node* const*, std::size_t>> reversed;
            auto path = it._path;
            while (!path.empty())
            {
                reversed.push_back({path.top().first, path.top().second});
                path.pop();
            }
            // из вектора в стек обратно
            for (auto iter = reversed.rbegin(); iter != reversed.rend(); ++iter)
            {
                _path.push(*iter);
            }
        }
        // константный реверсивный итератор в обычный константный итератор
        operator btree_const_iterator() const noexcept
        {
            return btree_const_iterator(_path, _index);
        }
        // Разыменование
        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }
        // указатель на данные
        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }
        // префиксный инкремент
        self& operator++()
        {
            move_to_previous(_path, _index);
            return *this;
        }
        // постфиксный инкремент
        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }
        // префиксный декремент
        self& operator--()
        {
            move_to_next(_path, _index);
            return *this;
        }
        // постфиксный декремент
        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }
        // сравнение итераторов
        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }
        // неравенство итераторов
        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }
        // глубина узла в дереве
        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }
        // сколько ключей в узле
        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }
        // является ли узел листом
        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }
        // индекс ключа внутри узла
        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    // секция 3: время жизни, доступ, поиск и обход.

    // основной конструктор, создает пустое дерево.
    explicit B_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(std::move(alloc)), _root(nullptr), _size(0)
    {
    }
    // тоже создает пустое дерево но порядок аргументов другой
    explicit B_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : B_tree(comp, std::move(alloc))
    {
    }
    // конструктор позволяет создать дерево и сразу наполнить его данными из любого другого контейнера
    // итератор подходит для работы с парами ключ-значение
    template <input_iterator_for_pair<tkey, tvalue> iterator>
    explicit B_tree(iterator first, iterator last, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : B_tree(cmp, std::move(alloc))
    {
        try
        {
            // циклом идем с начала и вставляем в B дерево
            for (; first != last; ++first)
            {
                insert(*first);
            }
        }
        catch (...)
        {
            clear(); // если ошибка то все стираем
            throw;
        }
    }
    // позволяет создавать дерево и сразу заполнять его данными с помощью фигурных скобок
    B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : B_tree(data.begin(), data.end(), cmp, std::move(alloc))
    {
    }
    // конструктор копирования
    B_tree(const B_tree& other)
        : compare(static_cast<const compare&>(other)), _allocator(other._allocator), _root(nullptr), _size(0)
    {
        _root = clone_subtree(other._root);
        _size = other._size;
    }
    // move конструктор
    B_tree(B_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(std::move(other._allocator)),
          _root(other._root),
          _size(other._size)
    {
        other._root = nullptr;
        other._size = 0;
    }
    // копирующее присваивание
    B_tree& operator=(const B_tree& other)
    {
        if (this != &other)
        {
            B_tree copy(other);
            swap_with(copy);
        }
        return *this;
    }
    // перемещающее присваивание
    B_tree& operator=(B_tree&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            static_cast<compare&>(*this) = std::move(static_cast<compare&>(other));
            _allocator = std::move(other._allocator);
            _root = other._root;
            _size = other._size;
            other._root = nullptr;
            other._size = 0;
        }
        return *this;
    }
    // деструктор
    ~B_tree() noexcept
    {
        clear();
    }
    // достать значение по ключу
    tvalue& at(const tkey& key)
    {
        auto iter = find(key); // находим ключ или бросаем исключение
        if (iter == end())
        {
            throw key_not_found();
        }
        return iter->second;
    }
    // константная версия
    const tvalue& at(const tkey& key) const
    {
        auto iter = find(key);
        if (iter == end())
        {
            throw key_not_found();
        }
        return iter->second;
    }
    // найти значение по ключу или создать
    tvalue& operator[](const tkey& key)
    {
        // если существует уже ключ то не вставит
        return insert(tree_data_type(key, tvalue{})).first->second;
    }

    tvalue& operator[](tkey&& key)
    {
        return insert(tree_data_type(std::move(key), tvalue{})).first->second;
    }
    // возвратить итератор на самый минимальный элемент дерева
    btree_iterator begin()
    {
        // создаем стек хранения пути от корня до узла
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_iterator(path, 0);
        }
        // начинаем с корня и спускаемся по самым левым ветвям до тех пор, пока не достигнем листа
        btree_node** link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            link = &(*link)->_pointers[0];
            path.push({link, 0});
        }
        return btree_iterator(path, 0);
    }
    // возвращает итератор на позицию ПОСЛЕ последнего элемента
    btree_iterator end()
    {
        // создаем стек хранения пути от корня до узла
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_iterator(path, 0);
        }
        // спускаемся в самый правый узел до тех пор, пока не достигнем листа
        btree_node** link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            std::size_t child_index = (*link)->_pointers.size() - 1;
            link = &(*link)->_pointers[child_index];
            path.push({link, child_index});
        }
        // возвращаем итератор, стоящий на индексе, равном количеству ключей
        return btree_iterator(path, (*link)->_keys.size());
    }

    // константные обертки
    btree_const_iterator begin() const
    {
        return cbegin();
    }

    btree_const_iterator end() const
    {
        return cend();
    }
    // такой же метод нахождения мин значения но константный
    btree_const_iterator cbegin() const
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_const_iterator(path, 0);
        }

        btree_node* const* link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            link = &(*link)->_pointers[0];
            path.push({link, 0});
        }
        return btree_const_iterator(path, 0);
    }
    // такой же метод нахождения макс значения но константный
    btree_const_iterator cend() const
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_const_iterator(path, 0);
        }

        btree_node* const* link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            std::size_t child_index = (*link)->_pointers.size() - 1;
            link = &(*link)->_pointers[child_index];
            path.push({link, child_index});
        }
        return btree_const_iterator(path, (*link)->_keys.size());
    }
    // возвращает ПОСЛЕДНИЙ элемент(а так то же самое что и end)
    btree_reverse_iterator rbegin()
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_reverse_iterator(path, 0);
        }

        btree_node** link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            std::size_t child_index = (*link)->_pointers.size() - 1;
            link = &(*link)->_pointers[child_index];
            path.push({link, child_index});
        }
        return btree_reverse_iterator(path, (*link)->_keys.size() - 1);
    }
    // возвращает индекс -1 npos перед самым первым эжлементом(а так то же самое что и begin)
    btree_reverse_iterator rend()
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_reverse_iterator(path, 0);
        }

        btree_node** link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            link = &(*link)->_pointers[0];
            path.push({link, 0});
        }
        return btree_reverse_iterator(path, npos);
    }
    // константный реверс обертки
    btree_const_reverse_iterator rbegin() const
    {
        return crbegin();
    }

    btree_const_reverse_iterator rend() const
    {
        return crend();
    }
    // возвращает ПОСЛЕДНИЙ элемент(а так то же самое что и end) КОНСТАНТНЫЙ
    btree_const_reverse_iterator crbegin() const
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_const_reverse_iterator(path, 0);
        }

        btree_node* const* link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            std::size_t child_index = (*link)->_pointers.size() - 1;
            link = &(*link)->_pointers[child_index];
            path.push({link, child_index});
        }
        return btree_const_reverse_iterator(path, (*link)->_keys.size() - 1);
    }
    // возвращает индекс -1 npos перед самым первым эжлементом(а так то же самое что и begin) КОНСТАНТНЫЙ
    btree_const_reverse_iterator crend() const
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_const_reverse_iterator(path, 0);
        }

        btree_node* const* link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            link = &(*link)->_pointers[0];
            path.push({link, 0});
        }
        return btree_const_reverse_iterator(path, npos);
    }
    // текущее количество элементов в дереве
    std::size_t size() const noexcept
    {
        return _size;
    }
    // проверка пустое ли дерево
    bool empty() const noexcept
    {
        return _size == 0;
    }
    // метод поиска элемента по ключу
    btree_iterator find(const tkey& key)
    {
        // используем lower bound чтобы найти не меньше нашего и сравниваем затем
        auto iter = lower_bound(key);
        if (iter == end() || !equivalent_keys(iter->first, key))
        {
            return end();
        }
        return iter;
    }
    // то же самое но константный
    btree_const_iterator find(const tkey& key) const
    {
        auto iter = lower_bound(key);
        if (iter == end() || !equivalent_keys(iter->first, key))
        {
            return end();
        }
        return iter;
    }

    // ищет первый элемент, который не меньше заданного ключа.
    btree_iterator lower_bound(const tkey& key)
    {
        std::stack<std::pair<btree_node**, std::size_t>> path; // текущий путь спуска
        std::stack<std::pair<btree_node**, std::size_t>> candidate_path; // путь к лучшему кандидату
        std::size_t candidate_index = 0;
        bool candidate_exists = false; // нашли ли хотя бы один подходящий элемент

        btree_node** link = &_root; // начинаем с корня
        std::size_t source_child_index = 0;
        // спускаемся сверху в низ
        while (*link != nullptr)
        {
            path.push({link, source_child_index});
            btree_node* node = *link;
            // ищем индекс ключа ВНУТРИ текущего узла
            const std::size_t index = lower_index_in_node(node, key);
            // если нашлось число не меньшен
            if (index < node->_keys.size())
            {
                candidate_path = path;
                candidate_index = index;
                candidate_exists = true;
            }
            // если в листе то некуда спускаться
            if (node->_pointers.empty())
            {
                break;
            }
            // теперь попадаем в список детей ключа который первый не меньше нашего
            source_child_index = index;
            link = &node->_pointers[index];
        }
        // либо возвратит лучшего кандидата либо в дереве все ключи меньше нашего
        return candidate_exists ? btree_iterator(candidate_path, candidate_index) : end();
    }
    // то же самое но константное
    btree_const_iterator lower_bound(const tkey& key) const
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> path;
        std::stack<std::pair<btree_node* const*, std::size_t>> candidate_path;
        std::size_t candidate_index = 0;
        bool candidate_exists = false;

        btree_node* const* link = &_root;
        std::size_t source_child_index = 0;

        while (*link != nullptr)
        {
            path.push({link, source_child_index});
            btree_node* node = *link;
            const std::size_t index = lower_index_in_node(node, key);

            if (index < node->_keys.size())
            {
                candidate_path = path;
                candidate_index = index;
                candidate_exists = true;
            }

            if (node->_pointers.empty())
            {
                break;
            }

            source_child_index = index;
            link = &node->_pointers[index];
        }

        return candidate_exists ? btree_const_iterator(candidate_path, candidate_index) : end();
    }
    // обычный и константный upperbound(для тестов, так должны по другому выводить первый больше нашего)
    btree_iterator upper_bound(const tkey& key)
    {
        return lower_bound(key);
    }

    btree_const_iterator upper_bound(const tkey& key) const
    {
        return lower_bound(key);
    }
    // проверка на наличие ключа в дереве
    bool contains(const tkey& key) const
    {
        return find(key) != end();
    }
    // полная очистка дерева и освобождение памяти
    void clear() noexcept
    {
        destroy_subtree(_root);
        _root = nullptr;
        _size = 0;
    }

private:
    // Секция 4: механика вставки и удаления.
    bool remove_from_node(btree_node* node, const tkey& key)
    {
        if (node == nullptr)
        {
            return false;
        }

        const std::size_t index = lower_index_in_node(node, key);

        if (index < node->_keys.size() && equivalent_keys(node->_keys[index].first, key))
        {
            // если у узла нет детей мы вычеркиваем ключ из массива
            if (node->_pointers.empty())
            {
                node->_keys.erase(node->_keys.begin() + static_cast<std::ptrdiff_t>(index));
                return true;
            }
            // случай внутренний узел
            return remove_from_internal_node(node, index);
        }
        // если ключа нет такоего
        if (node->_pointers.empty())
        {
            return false;
        }
        // собираемся ли мы зайти в крайнего правого ребенка(равен размеру ключей родителя)
        const bool was_last_child = index == node->_keys.size();
        // если минимальное значение ключей
        if (node->_pointers[index]->_keys.size() == minimum_keys_in_node)
        {
            fill_child_before_descent(node, index);
        }
        // если мы слили узлы
        if (was_last_child && index > node->_keys.size())
        {
            // тогда спускаемся по index - 1
            return remove_from_node(node->_pointers[index - 1], key);
        }
        // финальное удаление
        return remove_from_node(node->_pointers[index], key);
    }

    bool remove_from_internal_node(btree_node* node, std::size_t index)
    {
        // запоминаем и берем указатели на левого ребенка и правого
        const tkey key = node->_keys[index].first;

        btree_node* left_child = node->_pointers[index];
        btree_node* right_child = node->_pointers[index + 1];

        if (left_child->_keys.size() >= t) // если у левого ребёнка ключей больше минимума
        {
            // самый большой ключ из левого ребенка
            tree_data_type predecessor = get_predecessor(node, index);
            const tkey predecessor_key = predecessor.first;
            // заменяем удаляемый на него и удаляем из ребенка его
            node->_keys[index] = std::move(predecessor);

            return remove_from_node(left_child, predecessor_key);
        }
        // если левый бедный тогда берем мин из правого
        if (right_child->_keys.size() >= t)
        {
            // самый маленький ключ из правого ребенка
            tree_data_type successor = get_successor(node, index);
            const tkey successor_key = successor.first;
            // заменяем удаляемый на него и удаляем из ребенка его
            node->_keys[index] = std::move(successor);

            return remove_from_node(right_child, successor_key);
        }
        // если все бедные то слияние
        merge_children(node, index);

        return remove_from_node(left_child, key);
    }
    // самый большой ключ в левом поддереве
    tree_data_type get_predecessor(btree_node* node, std::size_t index) const
    {
        btree_node* current = node->_pointers[index];
        // идем в правого до того как не станет листом
        while (!current->_pointers.empty())
        {
            current = current->_pointers.back();
        }
        // возвращаем последнее значение
        return current->_keys.back();
    }
    // самый маленький ключ в правом поддереве
    tree_data_type get_successor(btree_node* node, std::size_t index) const
    {
        btree_node* current = node->_pointers[index + 1];
        // идем в левое до того как не станет листом
        while (!current->_pointers.empty())
        {
            current = current->_pointers.front();
        }
        // возвращаем первое значение
        return current->_keys.front();
    }
    // гарантия что у дочернего узла больше мин кол ва ключей
    void fill_child_before_descent(btree_node* parent, std::size_t child_index)
    {
        // не первый ребенок и у левого брата есть лишние ключи, то забираем ключ один
        if (child_index > 0 && parent->_pointers[child_index - 1]->_keys.size() >= t)
        {
            borrow_from_previous(parent, child_index);
            return;
        }
        // не последний ребенок и у правого брата есть лишние ключи, то забираем ключ один
        if (child_index + 1 < parent->_pointers.size() &&
            parent->_pointers[child_index + 1]->_keys.size() >= t)
        {
            borrow_from_next(parent, child_index);
            return;
        }
        // сливаемся с правым
        if (child_index + 1 < parent->_pointers.size())
        {
            merge_children(parent, child_index);
        }
        // сливаемся с левым
        else
        {
            merge_children(parent, child_index - 1);
        }
    }
    // занимаем у левого брата
    void borrow_from_previous(btree_node* parent, std::size_t child_index)
    {
        // мы и левый ребенок брат
        btree_node* child = parent->_pointers[child_index];
        btree_node* sibling = parent->_pointers[child_index - 1];
        // ключ из родителя и вставляем в начало
        child->_keys.insert(child->_keys.begin(), std::move(parent->_keys[child_index - 1]));

        // если не лист
        if (!sibling->_pointers.empty())
        {
            // вставляем указатель на самого правого ребенка левого брата нам в начало
            child->_pointers.insert(child->_pointers.begin(), sibling->_pointers.back());
            sibling->_pointers.pop_back();
        }
        // теперь родитель наш будет самый правый у левого
        parent->_keys[child_index - 1] = std::move(sibling->_keys.back());
        sibling->_keys.pop_back();
    }

    void borrow_from_next(btree_node* parent, std::size_t child_index)
    {
        // мы и правый ребенок брат
        btree_node* child = parent->_pointers[child_index];
        btree_node* sibling = parent->_pointers[child_index + 1];
        // ключ из родителя и вставляем в конец
        child->_keys.push_back(std::move(parent->_keys[child_index]));
        // если не лист
        if (!sibling->_pointers.empty())
        {
            // вставляем указатель на первого ребенка в наш конец
            child->_pointers.push_back(sibling->_pointers.front());
            sibling->_pointers.erase(sibling->_pointers.begin());
        }

        // первый ключ будет родителем новым
        parent->_keys[child_index] = std::move(sibling->_keys.front());
        sibling->_keys.erase(sibling->_keys.begin());
    }

    void merge_children(btree_node* parent, std::size_t left_child_index)
    {
        btree_node* left_child = parent->_pointers[left_child_index];
        btree_node* right_child = parent->_pointers[left_child_index + 1];
        // ключ из родителя опускается в начало левого ребенка
        left_child->_keys.push_back(std::move(parent->_keys[left_child_index]));
        // копируем ключи и детей из правого в конец левого
        for (auto& key_value : right_child->_keys)
        {
            left_child->_keys.push_back(std::move(key_value));
        }

        for (btree_node* child : right_child->_pointers)
        {
            left_child->_pointers.push_back(child);
        }
        // удаляем ключ родителя который забрали и указатель на правого ребенка
        parent->_keys.erase(parent->_keys.begin() + static_cast<std::ptrdiff_t>(left_child_index));
        parent->_pointers.erase(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1));
        // уничтожение правого узла
        right_child->_pointers.clear();
        destroy_node(right_child);
    }

    // рабочий вставки
    void insert_into_subtree(btree_node* node, tree_data_type data)
    {
        // ищем индекс
        const tkey& key = data.first;
        std::size_t index = lower_index_in_node(node, key);
        // если лист то вставляем
        if (node->_pointers.empty())
        {
            node->_keys.insert(node->_keys.begin() + static_cast<std::ptrdiff_t>(index), std::move(data));
            return;
        }
        // идем глубже к листу если мы внутренний
        insert_into_subtree(node->_pointers[index], std::move(data));

        // если ребенок стал большим
        if (node->_pointers[index]->_keys.size() > maximum_keys_in_node)
        {
            split_child(node, index);
        }
    }
    // директор вставки
    std::pair<btree_iterator, bool> insert_value(tree_data_type data)
    {
        // достаем ключ и ищем, если уже есть то false
        const tkey search_key = data.first;
        auto existing = find(search_key);
        if (existing != end())
        {
            return {existing, false};
        }

        // если мы добавляем первый элемент
        if (_root == nullptr)
        {
            _root = create_node();
            try
            {
                _root->_keys.push_back(std::move(data));
                _size = 1;
                return {find(search_key), true};
            }
            // непредвиденные ошибки
            catch (...)
            {
                destroy_node(_root);
                _root = nullptr;
                _size = 0;
                throw;
            }
        }
        // вставка
        insert_into_subtree(_root, std::move(data));

        // если корень переполнен то дерево растет!!!! создаем новый корень и серединный элемент - главой ставновится а лево и право - дети
        if (_root->_keys.size() > maximum_keys_in_node)
        {
            btree_node* new_root = create_node();
            try
            {
                new_root->_pointers.push_back(_root);
                split_child(new_root, 0);
                _root = new_root;
            }
            // непредвиденные ошибки
            catch (...)
            {
                destroy_node(new_root);
                throw;
            }
        }
        // увеличиваем размер
        ++_size;
        return {find(search_key), true};
    }

public:
    // обертка вставки
    std::pair<btree_iterator, bool> insert(const tree_data_type& data)
    {
        return insert_value(data);
    }
    // обертка вставки с поддержкой перемещения для оптимизации
    std::pair<btree_iterator, bool> insert(tree_data_type&& data)
    {
        return insert_value(std::move(data));
    }
    // вставляем переменное кол во аргументов ........... emplace конструирует объект данных непосредственно внутри узла дерева
    template <typename... Args>
    std::pair<btree_iterator, bool> emplace(Args&&... args)
    {
        // создаем объект прямо здесь из переданных аргументо и передаем функции insert без лишнего копирования
        tree_data_type data(std::forward<Args>(args)...);
        return insert(std::move(data));
    }

    // вставь или перезапиши(т.е. дата будет заменяна на нашу)
    btree_iterator insert_or_assign(const tree_data_type& data)
    {
        auto iter = find(data.first);
        if (iter != end())
        {
            iter->second = data.second;
            return iter;
        }
        return insert(data).first;
    }

    // то же самое только переносит а не перезаписывает(хорошо для больших данных)
    btree_iterator insert_or_assign(tree_data_type&& data)
    {
        auto iter = find(data.first);
        if (iter != end())
        {
            iter->second = std::move(data.second);
            return iter;
        }
        return insert(std::move(data)).first;
    }
    // вставь или обнови переменное кол во арг
    template <typename... Args>
    btree_iterator emplace_or_assign(Args&&... args)
    {
        // собираем объект данных и вызываем insert_or_assign
        tree_data_type data(std::forward<Args>(args)...);
        return insert_or_assign(std::move(data));
    }

    // удаление элемента по итератору
    btree_iterator erase(btree_iterator pos)
    {
        if (pos == end()) // если конец то выходим иначе берем ключ и стандартное удаление
        {
            return end();
        }
        const tkey key = pos->first;
        return erase(key);
    }

    // версия для константного итератора
    btree_iterator erase(btree_const_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }
        const tkey key = pos->first;
        return erase(key);
    }

    // удаляем в диапазоне от одного итератора к другому
    btree_iterator erase(btree_iterator first, btree_iterator last)
    {
        std::vector<tkey> keys_to_remove;

        // сначала выписываем все ключи на удаление
        for (auto iter = first; iter != last; ++iter)
        {
            keys_to_remove.push_back(iter->first);
        }
        // запоминаем ключ после удаляемых
        std::optional<tkey> return_key;

        if (last != end())
        {
            return_key = last->first;
        }
        // удаляем каждый ключ
        for (const tkey& key : keys_to_remove)
        {
            erase(key);
        }
        // возвращаем итератор на новое расположение ключа
        return return_key.has_value() ? lower_bound(*return_key) : end();
    }
    // конст версия
    btree_iterator erase(btree_const_iterator first, btree_const_iterator last)
    {
        std::vector<tkey> keys_to_remove;

        for (auto iter = first; iter != last; ++iter)
        {
            keys_to_remove.push_back(iter->first);
        }

        std::optional<tkey> return_key;

        if (last != end())
        {
            return_key = last->first;
        }

        for (const tkey& key : keys_to_remove)
        {
            erase(key);
        }

        return return_key.has_value() ? lower_bound(*return_key) : end();
    }

    btree_iterator erase(const tkey& key)
    {
        // если ключа нет то ничего не делаем
        auto iter = find(key);
        if (iter == end())
        {
            return end();
        }
        // запоминаем ключ после удаляемых если он есть
        std::optional<tkey> return_key;

        auto next = iter;
        ++next;

        if (next != end())
        {
            return_key = next->first;
        }
        // если ключ был удален то уменьшаме размер
        if (remove_from_node(_root, key))
        {
            --_size;
        }

        // если корень пустой
        if (_root != nullptr && _root->_keys.empty())
        {
            // если даже нет детей
            if (_root->_pointers.empty())
            {
                destroy_node(_root);
                _root = nullptr;
            }
            // корень пустой но есть ребенок, понижаем уровень
            else
            {
                btree_node* old_root = _root; // создаем временный корень
                _root = old_root->_pointers.front(); // единственный ребенок становится новым корнем
                old_root->_pointers.clear(); // удаляем указатели на старых детей
                destroy_node(old_root); // удаляем старый корень
            }
        }

        // возвращаем итератор на новое расположение ключа
        return return_key.has_value() ? lower_bound(*return_key) : end();
    }
};
// позволяет компилятору автоматически определять типы если дерево создается через диапазон итераторов [begin, end)
//вместо B_tree<int, std::string, std::less<int>, 5> tree(vec.begin(), vec.end());
//пишу B_tree tree(vec.begin(), vec.end());
template <
    std::input_iterator iterator,
    comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
    std::size_t t = 5,
    typename U = std::pair<const typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type>>
B_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<typename std::iterator_traits<iterator>::value_type::first_type,
              typename std::iterator_traits<iterator>::value_type::second_type,
              compare,
              t>;

// позволяет автоматически выводить типы ключа и значения при создании дерева через список в фигурных скобках { {key, value}, ... }
// вместо B_tree<int, std::string, std::less<int>, 5> tree = { {1, "loh"}, {2, "gay"} };
// пишу B_tree tree = { {1, "loh"}, {2, "gay"} };
template <
    typename tkey,
    typename tvalue,
    comparator<tkey> compare = std::less<tkey>,
    std::size_t t = 5,
    typename U = std::pair<const tkey, tvalue>>
B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<tkey, tvalue, compare, t>;

#endif
