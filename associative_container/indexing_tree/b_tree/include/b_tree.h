#ifndef SYS_PROG_B_TREE_H
#define SYS_PROG_B_TREE_H

#include <iterator>
#include <utility>
#include <boost/container/static_vector.hpp>
#include <stack>
#include <pp_allocator.h>
#include <associative_container.h>
#include <not_implemented.h>
#include <initializer_list>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class B_tree final : private compare // EBCO
{
public:

    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:

    static constexpr const size_t minimum_keys_in_node = t - 1;
    static constexpr const size_t maximum_keys_in_node = 2 * t - 1;

    // region comparators declaration

    inline bool compare_keys(const tkey& lhs, const tkey& rhs) const;
    inline bool compare_pairs(const tree_data_type& lhs, const tree_data_type& rhs) const;

    // endregion comparators declaration


    struct btree_node
    {
        boost::container::static_vector<tree_data_type, maximum_keys_in_node + 1> _keys;
        boost::container::static_vector<btree_node*, maximum_keys_in_node + 2> _pointers;
        btree_node() noexcept;
    };

    pp_allocator<value_type> _allocator;
    btree_node* _root;
    size_t _size;

    pp_allocator<value_type> get_allocator() const noexcept;

public:

    // region constructors declaration

    explicit B_tree(const compare& cmp = compare(), pp_allocator<value_type> = pp_allocator<value_type>());

    explicit B_tree(pp_allocator<value_type> alloc, const compare& comp = compare());

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit B_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<value_type> = pp_allocator<value_type>());

    B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> = pp_allocator<value_type>());

    // endregion constructors declaration

    // region five declaration

    B_tree(const B_tree& other);

    B_tree(B_tree&& other) noexcept;

    B_tree& operator=(const B_tree& other);

    B_tree& operator=(B_tree&& other) noexcept;

    ~B_tree() noexcept;

    // endregion five declaration

    // region iterators declaration

    class btree_iterator;
    class btree_reverse_iterator;
    class btree_const_iterator;
    class btree_const_reverse_iterator;

    class btree_iterator final
    {
        std::stack<std::pair<btree_node**, size_t>> _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_iterator(const std::stack<std::pair<btree_node**, size_t>>& path = std::stack<std::pair<btree_node**, size_t>>(), size_t index = 0);

    };

    class btree_const_iterator final
    {
        std::stack<std::pair<btree_node* const*, size_t>> _path;
        size_t _index;

    public:

        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_const_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_iterator;
        friend class btree_const_reverse_iterator;

        btree_const_iterator(const btree_iterator& it) noexcept;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_const_iterator(const std::stack<std::pair<btree_node* const*, size_t>>& path = std::stack<std::pair<btree_node* const*, size_t>>(), size_t index = 0);
    };

    class btree_reverse_iterator final
    {
        std::stack<std::pair<btree_node**, size_t>> _path;
        size_t _index;

    public:

        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_reverse_iterator;

        friend class B_tree;
        friend class btree_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;

        btree_reverse_iterator(const btree_iterator& it) noexcept;
        operator btree_iterator() const noexcept;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_reverse_iterator(const std::stack<std::pair<btree_node**, size_t>>& path = std::stack<std::pair<btree_node**, size_t>>(), size_t index = 0);
    };

    class btree_const_reverse_iterator final
    {
        std::stack<std::pair<btree_node* const*, size_t>> _path;
        size_t _index;

    public:

        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_const_reverse_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_iterator;

        btree_const_reverse_iterator(const btree_reverse_iterator& it) noexcept;
        operator btree_const_iterator() const noexcept;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_const_reverse_iterator(const std::stack<std::pair<btree_node* const*, size_t>>& path = std::stack<std::pair<btree_node* const*, size_t>>(), size_t index = 0);
    };

    friend class btree_iterator;
    friend class btree_const_iterator;
    friend class btree_reverse_iterator;
    friend class btree_const_reverse_iterator;

    // endregion iterators declaration

    // region element access declaration

    /*
     * Returns a reference to the mapped value of the element with specified key. If no such element exists, an exception of type std::out_of_range is thrown.
     */
    tvalue& at(const tkey&);
    const tvalue& at(const tkey&) const;

    /*
     * If key not exists, makes default initialization of value
     */
    tvalue& operator[](const tkey& key);
    tvalue& operator[](tkey&& key);

    // endregion element access declaration
    // region iterator begins declaration

    btree_iterator begin();
    btree_iterator end();

    btree_const_iterator begin() const;
    btree_const_iterator end() const;

    btree_const_iterator cbegin() const;
    btree_const_iterator cend() const;

    btree_reverse_iterator rbegin();
    btree_reverse_iterator rend();

    btree_const_reverse_iterator rbegin() const;
    btree_const_reverse_iterator rend() const;

    btree_const_reverse_iterator crbegin() const;
    btree_const_reverse_iterator crend() const;

    // endregion iterator begins declaration

    // region lookup declaration

    size_t size() const noexcept;
    bool empty() const noexcept;

    /*
     * Returns end() if not exist
     */

    btree_iterator find(const tkey& key);
    btree_const_iterator find(const tkey& key) const;

    btree_iterator lower_bound(const tkey& key);
    btree_const_iterator lower_bound(const tkey& key) const;

    btree_iterator upper_bound(const tkey& key);
    btree_const_iterator upper_bound(const tkey& key) const;

    bool contains(const tkey& key) const;

    // endregion lookup declaration

    // region modifiers declaration

    void clear() noexcept;

    /*
     * Does nothing if key exists, delegates to emplace.
     * Second return value is true, when inserted
     */
    std::pair<btree_iterator, bool> insert(const tree_data_type& data);
    std::pair<btree_iterator, bool> insert(tree_data_type&& data);

    template <typename ...Args>
    std::pair<btree_iterator, bool> emplace(Args&&... args);

    /*
     * Updates value if key exists, delegates to emplace.
     */
    btree_iterator insert_or_assign(const tree_data_type& data);
    btree_iterator insert_or_assign(tree_data_type&& data);

    template <typename ...Args>
    btree_iterator emplace_or_assign(Args&&... args);

    /*
     * Return iterator to node next ro removed or end() if key not exists
     */
    btree_iterator erase(btree_iterator pos);
    btree_iterator erase(btree_const_iterator pos);

    btree_iterator erase(btree_iterator beg, btree_iterator en);
    btree_iterator erase(btree_const_iterator beg, btree_const_iterator en);


    btree_iterator erase(const tkey& key);

    // endregion modifiers declaration
};

template<std::input_iterator iterator, comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
        std::size_t t = 5, typename U>
B_tree(iterator begin, iterator end, const compare &cmp = compare(), pp_allocator<U> = pp_allocator<U>()) -> B_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare &cmp = compare(), pp_allocator<U> = pp_allocator<U>()) -> B_tree<tkey, tvalue, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::compare_pairs(const B_tree::tree_data_type &lhs,
                                                     const B_tree::tree_data_type &rhs) const
{
    return compare_keys(lhs.first, rhs.first);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::compare_keys(const tkey &lhs, const tkey &rhs) const
{
    return compare::operator()(lhs, rhs);
}



template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_node::btree_node() noexcept : _keys(), _pointers()
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
pp_allocator<typename B_tree<tkey, tvalue, compare, t>::value_type> B_tree<tkey, tvalue, compare, t>::get_allocator() const noexcept
{
    return _allocator;
}

// region constructors implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::B_tree(
        const compare& cmp,
        pp_allocator<value_type> alloc)
    : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::B_tree(
        pp_allocator<value_type> alloc,
        const compare& comp)
    : compare(comp), _allocator(alloc), _root(nullptr), _size(0)
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
template<input_iterator_for_pair<tkey, tvalue> iterator>
B_tree<tkey, tvalue, compare, t>::B_tree(
        iterator begin,
        iterator end,
        const compare& cmp,
        pp_allocator<value_type> alloc)
    : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
{
    for (auto it = begin; it != end; ++it) {
        insert(*it);
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::B_tree(
        std::initializer_list<std::pair<tkey, tvalue>> data,
        const compare& cmp,
        pp_allocator<value_type> alloc)
    : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
{
    for (const auto& item : data) {
        insert(item);
    }
}

// endregion constructors implementation

// region five implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::~B_tree() noexcept
{
    clear();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::B_tree(const B_tree& other)
    : compare(other), _allocator(other.get_allocator().select_on_container_copy_construction()), _root(nullptr), _size(0)
{
    for (auto it = other.cbegin(); it != other.cend(); ++it) {
        insert(*it);
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>& B_tree<tkey, tvalue, compare, t>::operator=(const B_tree& other)
{
    if (this != &other) {
        clear();
        compare::operator=(other);
        for (auto it = other.cbegin(); it != other.cend(); ++it) {
            insert(*it);
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::B_tree(B_tree&& other) noexcept
    : compare(std::move(other)), _allocator(std::move(other._allocator)), _root(other._root), _size(other._size)
{
    other._root = nullptr;
    other._size = 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>& B_tree<tkey, tvalue, compare, t>::operator=(B_tree&& other) noexcept
{
    if (this != &other) {
        clear();
        compare::operator=(std::move(other));
        _allocator = std::move(other._allocator);
        _root = other._root;
        _size = other._size;
        other._root = nullptr;
        other._size = 0;
    }
    return *this;
}

// endregion five implementation

// region iterators implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_iterator::btree_iterator(
        const std::stack<std::pair<btree_node**, size_t>>& path, size_t index)
    : _path(path), _index(index)
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator*() const noexcept
{
    return reinterpret_cast<reference>((*_path.top().first)->_keys[_index]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator->() const noexcept
{
    return reinterpret_cast<pointer>(&((*_path.top().first)->_keys[_index]));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator&
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator++()
{
    if (_path.empty()) return *this;
    auto node = *_path.top().first;
    if (!node->_pointers.empty()) {
        _path.push({&node->_pointers[_index + 1], _index + 1});
        while (!(*_path.top().first)->_pointers.empty()) {
            auto child = *_path.top().first;
            _path.push({&child->_pointers[0], 0});
        }
        _index = 0;
    } else {
        ++_index;
        while (!_path.empty()) {
            if (_index < (*_path.top().first)->_keys.size()) break;
            size_t child_idx = _path.top().second;
            _path.pop();
            _index = child_idx;
        }
        if (_path.empty()) _index = 0;
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator++(int)
{
    auto copy = *this;
    ++(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator&
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator--()
{
    if (_path.empty()) return *this;
    auto node = *_path.top().first;
    if (!node->_pointers.empty()) {
        _path.push({&node->_pointers[_index], _index});
        while (!(*_path.top().first)->_pointers.empty()) {
            auto child = *_path.top().first;
            _path.push({&child->_pointers[child->_keys.size()], child->_keys.size()});
        }
        _index = (*_path.top().first)->_keys.size() - 1;
    } else {
        if (_index > 0) {
            --_index;
        } else {
            while (!_path.empty()) {
                size_t child_idx = _path.top().second;
                _path.pop();
                if (child_idx > 0) {
                    _index = child_idx - 1;
                    break;
                }
            }
            if (_path.empty()) _index = 0;
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator--(int)
{
    auto copy = *this;
    --(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_iterator::operator==(const self& other) const noexcept
{
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() != other._path.empty()) return false;
    return _path.top().first == other._path.top().first && _index == other._index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_iterator::depth() const noexcept
{
    return _path.size() > 0 ? _path.size() - 1 : 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return false;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_iterator::index() const noexcept
{
    return _index;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::btree_const_iterator(
        const std::stack<std::pair<btree_node* const*, size_t>>& path, size_t index)
    : _path(path), _index(index)
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::btree_const_iterator(
        const btree_iterator& it) noexcept
    : _index(it._index)
{
    std::stack<std::pair<btree_node* const*, size_t>> temp_path;
    auto orig_path = it._path;
    std::vector<std::pair<btree_node**, size_t>> rev;
    while (!orig_path.empty()) {
        rev.push_back(orig_path.top());
        orig_path.pop();
    }
    for (auto i = rev.rbegin(); i != rev.rend(); ++i) {
        _path.push({i->first, i->second});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator*() const noexcept
{
    return reinterpret_cast<reference>((*_path.top().first)->_keys[_index]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator->() const noexcept
{
    return reinterpret_cast<pointer>(&((*_path.top().first)->_keys[_index]));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator++()
{
    if (_path.empty()) return *this;
    auto node = *_path.top().first;
    if (!node->_pointers.empty()) {
        _path.push({&node->_pointers[_index + 1], _index + 1});
        while (!(*_path.top().first)->_pointers.empty()) {
            auto child = *_path.top().first;
            _path.push({&child->_pointers[0], 0});
        }
        _index = 0;
    } else {
        ++_index;
        while (!_path.empty()) {
            if (_index < (*_path.top().first)->_keys.size()) break;
            size_t child_idx = _path.top().second;
            _path.pop();
            _index = child_idx;
        }
        if (_path.empty()) _index = 0;
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator++(int)
{
    auto copy = *this;
    ++(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator--()
{
    if (_path.empty()) return *this;
    auto node = *_path.top().first;
    if (!node->_pointers.empty()) {
        _path.push({&node->_pointers[_index], _index});
        while (!(*_path.top().first)->_pointers.empty()) {
            auto child = *_path.top().first;
            _path.push({&child->_pointers[child->_keys.size()], child->_keys.size()});
        }
        _index = (*_path.top().first)->_keys.size() - 1;
    } else {
        if (_index > 0) {
            --_index;
        } else {
            while (!_path.empty()) {
                size_t child_idx = _path.top().second;
                _path.pop();
                if (child_idx > 0) {
                    _index = child_idx - 1;
                    break;
                }
            }
            if (_path.empty()) _index = 0;
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator--(int)
{
    auto copy = *this;
    --(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator==(const self& other) const noexcept
{
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() != other._path.empty()) return false;
    return _path.top().first == other._path.top().first && _index == other._index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_iterator::depth() const noexcept
{
    return _path.size() > 0 ? _path.size() - 1 : 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return false;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_iterator::index() const noexcept
{
    return _index;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::btree_reverse_iterator(
        const std::stack<std::pair<btree_node**, size_t>>& path, size_t index)
    : _path(path), _index(index)
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::btree_reverse_iterator(
        const btree_iterator& it) noexcept
    : _path(it._path), _index(it._index)
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator B_tree<tkey, tvalue, compare, t>::btree_iterator() const noexcept
{
    return btree_iterator(_path, _index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator*() const noexcept
{
    auto tmp = btree_iterator(_path, _index);
    return *tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator->() const noexcept
{
    auto tmp = btree_iterator(_path, _index);
    return &(*tmp);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator++()
{
    auto tmp = btree_iterator(_path, _index);
    --tmp;
    _path = tmp._path;
    _index = tmp._index;
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator++(int)
{
    auto copy = *this;
    ++(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator--()
{
    auto tmp = btree_iterator(_path, _index);
    ++tmp;
    _path = tmp._path;
    _index = tmp._index;
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator--(int)
{
    auto copy = *this;
    --(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator==(const self& other) const noexcept
{
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() != other._path.empty()) return false;
    return _path.top().first == other._path.top().first && _index == other._index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::depth() const noexcept
{
    return _path.size() > 0 ? _path.size() - 1 : 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return false;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::index() const noexcept
{
    return _index;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::btree_const_reverse_iterator(
        const std::stack<std::pair<btree_node* const*, size_t>>& path, size_t index)
    : _path(path), _index(index)
{
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::btree_const_reverse_iterator(
        const btree_reverse_iterator& it) noexcept
    : _index(it._index)
{
    std::stack<std::pair<btree_node* const*, size_t>> temp_path;
    auto orig_path = it._path;
    std::vector<std::pair<btree_node**, size_t>> rev;
    while (!orig_path.empty()) {
        rev.push_back(orig_path.top());
        orig_path.pop();
    }
    for (auto i = rev.rbegin(); i != rev.rend(); ++i) {
        _path.push({i->first, i->second});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator B_tree<tkey, tvalue, compare, t>::btree_const_iterator() const noexcept
{
    return btree_const_iterator(_path, _index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator*() const noexcept
{
    auto tmp = btree_const_iterator(_path, _index);
    return *tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator->() const noexcept
{
    auto tmp = btree_const_iterator(_path, _index);
    return &(*tmp);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator++()
{
    auto tmp = btree_const_iterator(_path, _index);
    --tmp;
    _path = tmp._path;
    _index = tmp._index;
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator++(int)
{
    auto copy = *this;
    ++(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator--()
{
    auto tmp = btree_const_iterator(_path, _index);
    ++tmp;
    _path = tmp._path;
    _index = tmp._index;
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator--(int)
{
    auto copy = *this;
    --(*this);
    return copy;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator==(const self& other) const noexcept
{
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() != other._path.empty()) return false;
    return _path.top().first == other._path.top().first && _index == other._index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::depth() const noexcept
{
    return _path.size() > 0 ? _path.size() - 1 : 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return false;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::index() const noexcept
{
    return _index;
}

// endregion iterators implementation

// region iterator begins implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator B_tree<tkey, tvalue, compare, t>::begin()
{
    if (!_root) return end();
    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    while (!(*path.top().first)->_pointers.empty()) {
        auto child = *path.top().first;
        path.push({&child->_pointers[0], 0});
    }
    return btree_iterator(path, 0);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator B_tree<tkey, tvalue, compare, t>::end()
{
    return btree_iterator();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator B_tree<tkey, tvalue, compare, t>::begin() const
{
    return cbegin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator B_tree<tkey, tvalue, compare, t>::end() const
{
    return cend();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator B_tree<tkey, tvalue, compare, t>::cbegin() const
{
    if (!_root) return cend();
    std::stack<std::pair<btree_node* const*, size_t>> path;
    path.push({&_root, 0});
    while (!(*path.top().first)->_pointers.empty()) {
        auto child = *path.top().first;
        path.push({&child->_pointers[0], 0});
    }
    return btree_const_iterator(path, 0);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator B_tree<tkey, tvalue, compare, t>::cend() const
{
    return btree_const_iterator();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator B_tree<tkey, tvalue, compare, t>::rbegin()
{
    if (!_root) return rend();
    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    while (!(*path.top().first)->_pointers.empty()) {
        auto child = *path.top().first;
        path.push({&child->_pointers[child->_keys.size()], child->_keys.size()});
    }
    return btree_reverse_iterator(path, (*path.top().first)->_keys.size() - 1);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator B_tree<tkey, tvalue, compare, t>::rend()
{
    return btree_reverse_iterator();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator B_tree<tkey, tvalue, compare, t>::rbegin() const
{
    return crbegin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator B_tree<tkey, tvalue, compare, t>::rend() const
{
    return crend();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator B_tree<tkey, tvalue, compare, t>::crbegin() const
{
    if (!_root) return crend();
    std::stack<std::pair<btree_node* const*, size_t>> path;
    path.push({&_root, 0});
    while (!(*path.top().first)->_pointers.empty()) {
        auto child = *path.top().first;
        path.push({&child->_pointers[child->_keys.size()], child->_keys.size()});
    }
    return btree_const_reverse_iterator(path, (*path.top().first)->_keys.size() - 1);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator B_tree<tkey, tvalue, compare, t>::crend() const
{
    return btree_const_reverse_iterator();
}

// endregion iterator begins implementation

// region element access implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& B_tree<tkey, tvalue, compare, t>::at(const tkey& key)
{
    auto it = find(key);
    if (it == end()) throw std::out_of_range("Key not found in B-tree");
    return const_cast<tvalue&>(it->second);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
const tvalue& B_tree<tkey, tvalue, compare, t>::at(const tkey& key) const
{
    auto it = find(key);
    if (it == end()) throw std::out_of_range("Key not found in B-tree");
    return it->second;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& B_tree<tkey, tvalue, compare, t>::operator[](const tkey& key)
{
    auto it = find(key);
    if (it == end()) {
        it = insert({key, tvalue()}).first;
    }
    return const_cast<tvalue&>(it->second);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& B_tree<tkey, tvalue, compare, t>::operator[](tkey&& key)
{
    auto it = find(key);
    if (it == end()) {
        it = insert({std::move(key), tvalue()}).first;
    }
    return const_cast<tvalue&>(it->second);
}

// endregion element access implementation

// region lookup implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::size() const noexcept
{
    return _size;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::empty() const noexcept
{
    return _size == 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator B_tree<tkey, tvalue, compare, t>::find(const tkey& key)
{
    if (!_root) return end();
    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    while (true) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && compare_keys(node->_keys[i].first, key)) {
            ++i;
        }
        if (i < node->_keys.size() && !compare_keys(key, node->_keys[i].first)) {
            return btree_iterator(path, i);
        }
        if (node->_pointers.empty()) {
            return end();
        }
        path.push({&node->_pointers[i], i});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator B_tree<tkey, tvalue, compare, t>::find(const tkey& key) const
{
    if (!_root) return cend();
    std::stack<std::pair<btree_node* const*, size_t>> path;
    path.push({&_root, 0});
    while (true) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && compare_keys(node->_keys[i].first, key)) {
            ++i;
        }
        if (i < node->_keys.size() && !compare_keys(key, node->_keys[i].first)) {
            return btree_const_iterator(path, i);
        }
        if (node->_pointers.empty()) {
            return cend();
        }
        path.push({&node->_pointers[i], i});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator B_tree<tkey, tvalue, compare, t>::lower_bound(const tkey& key)
{
    auto it = end();
    if (!_root) return it;
    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    while (true) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && compare_keys(node->_keys[i].first, key)) {
            ++i;
        }
        if (i < node->_keys.size()) {
            it = btree_iterator(path, i);
            if (!compare_keys(key, node->_keys[i].first)) {
                return it; // exact match
            }
        }
        if (node->_pointers.empty()) {
            return it;
        }
        path.push({&node->_pointers[i], i});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator B_tree<tkey, tvalue, compare, t>::lower_bound(const tkey& key) const
{
    auto it = cend();
    if (!_root) return it;
    std::stack<std::pair<btree_node* const*, size_t>> path;
    path.push({&_root, 0});
    while (true) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && compare_keys(node->_keys[i].first, key)) {
            ++i;
        }
        if (i < node->_keys.size()) {
            it = btree_const_iterator(path, i);
            if (!compare_keys(key, node->_keys[i].first)) {
                return it; // exact match
            }
        }
        if (node->_pointers.empty()) {
            return it;
        }
        path.push({&node->_pointers[i], i});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator B_tree<tkey, tvalue, compare, t>::upper_bound(const tkey& key)
{
    auto it = end();
    if (!_root) return it;
    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    while (true) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && !compare_keys(key, node->_keys[i].first)) {
            ++i;
        }
        if (i < node->_keys.size()) {
            it = btree_iterator(path, i);
        }
        if (node->_pointers.empty()) {
            return it;
        }
        path.push({&node->_pointers[i], i});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator B_tree<tkey, tvalue, compare, t>::upper_bound(const tkey& key) const
{
    auto it = cend();
    if (!_root) return it;
    std::stack<std::pair<btree_node* const*, size_t>> path;
    path.push({&_root, 0});
    while (true) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && !compare_keys(key, node->_keys[i].first)) {
            ++i;
        }
        if (i < node->_keys.size()) {
            it = btree_const_iterator(path, i);
        }
        if (node->_pointers.empty()) {
            return it;
        }
        path.push({&node->_pointers[i], i});
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::contains(const tkey& key) const
{
    return find(key) != end();
}

// endregion lookup implementation

// region modifiers implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
void B_tree<tkey, tvalue, compare, t>::clear() noexcept
{
    if (!_root) return;
    std::stack<btree_node*> nodes;
    nodes.push(_root);
    while (!nodes.empty()) {
        auto node = nodes.top();
        nodes.pop();
        for (auto child : node->_pointers) {
            if (child) nodes.push(child);
        }
        for (size_t i = 0; i < node->_keys.size(); ++i) {
            _allocator.destroy(&node->_keys[i]);
        }
        auto node_alloc = _allocator.template select_on_container_copy_construction();
        auto byte_allocator = pp_allocator<btree_node>(_allocator.resource());
        byte_allocator.destroy(node);
        byte_allocator.deallocate(node, 1);
    }
    _root = nullptr;
    _size = 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
std::pair<typename B_tree<tkey, tvalue, compare, t>::btree_iterator, bool>
B_tree<tkey, tvalue, compare, t>::insert(const tree_data_type& data)
{
    return emplace(data);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
std::pair<typename B_tree<tkey, tvalue, compare, t>::btree_iterator, bool>
B_tree<tkey, tvalue, compare, t>::insert(tree_data_type&& data)
{
    return emplace(std::move(data));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
template<typename... Args>
std::pair<typename B_tree<tkey, tvalue, compare, t>::btree_iterator, bool>
B_tree<tkey, tvalue, compare, t>::emplace(Args&&... args)
{
    auto temp_alloc = pp_allocator<tree_data_type>(_allocator.resource());
    tree_data_type* temp = temp_alloc.allocate(1);
    try {
        temp_alloc.construct(temp, std::forward<Args>(args)...);
    } catch (...) {
        temp_alloc.deallocate(temp, 1);
        throw;
    }

    tkey key = temp->first;
    auto it = find(key);
    if (it != end()) {
        temp_alloc.destroy(temp);
        temp_alloc.deallocate(temp, 1);
        return {it, false};
    }

    auto byte_allocator = pp_allocator<btree_node>(_allocator.resource());

    if (!_root) {
        _root = byte_allocator.allocate(1);
        byte_allocator.construct(_root);
        _root->_keys.push_back(std::move(*temp));
        temp_alloc.destroy(temp);
        temp_alloc.deallocate(temp, 1);
        _size++;
        return {find(key), true};
    }

    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    while (!(*path.top().first)->_pointers.empty()) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && compare_keys(node->_keys[i].first, key)) {
            ++i;
        }
        path.top().second = i;
        path.push({&node->_pointers[i], i});
    }

    auto leaf = *path.top().first;
    size_t i = 0;
    while (i < leaf->_keys.size() && compare_keys(leaf->_keys[i].first, key)) {
        ++i;
    }

    leaf->_keys.insert(leaf->_keys.begin() + i, std::move(*temp));
    temp_alloc.destroy(temp);
    temp_alloc.deallocate(temp, 1);
    _size++;

    while (!path.empty()) {
        auto node_ptr_ptr = path.top().first;
        auto node = *node_ptr_ptr;

        if (node->_keys.size() <= maximum_keys_in_node) {
            break;
        }

        size_t mid = t;
        btree_node* right = byte_allocator.allocate(1);
        byte_allocator.construct(right);

        for (size_t j = mid + 1; j < node->_keys.size(); ++j) {
            right->_keys.push_back(std::move(node->_keys[j]));
        }
        if (!node->_pointers.empty()) {
            for (size_t j = mid + 1; j < node->_pointers.size(); ++j) {
                right->_pointers.push_back(node->_pointers[j]);
            }
            node->_pointers.resize(mid + 1);
        }

        tree_data_type mid_val = std::move(node->_keys[mid]);
        node->_keys.resize(mid);

        path.pop();
        if (path.empty()) {
            btree_node* new_root = byte_allocator.allocate(1);
            byte_allocator.construct(new_root);
            new_root->_keys.push_back(std::move(mid_val));
            new_root->_pointers.push_back(node);
            new_root->_pointers.push_back(right);
            _root = new_root;
        } else {
            auto parent = *path.top().first;
            size_t insert_idx = path.top().second;
            parent->_keys.insert(parent->_keys.begin() + insert_idx, std::move(mid_val));
            parent->_pointers.insert(parent->_pointers.begin() + insert_idx + 1, right);
        }
    }

    return {find(key), true};
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::insert_or_assign(const tree_data_type& data)
{
    auto it = find(data.first);
    if (it != end()) {
        const_cast<tvalue&>(it->second) = data.second;
        return it;
    }
    return insert(data).first;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::insert_or_assign(tree_data_type&& data)
{
    auto it = find(data.first);
    if (it != end()) {
        const_cast<tvalue&>(it->second) = std::move(data.second);
        return it;
    }
    return insert(std::move(data)).first;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
template<typename... Args>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::emplace_or_assign(Args&&... args)
{
    auto temp_alloc = pp_allocator<tree_data_type>(_allocator.resource());
    tree_data_type* temp = temp_alloc.allocate(1);
    try {
        temp_alloc.construct(temp, std::forward<Args>(args)...);
    } catch (...) {
        temp_alloc.deallocate(temp, 1);
        throw;
    }

    tkey key = temp->first;
    auto it = find(key);
    if (it != end()) {
        const_cast<tvalue&>(it->second) = std::move(temp->second);
        temp_alloc.destroy(temp);
        temp_alloc.deallocate(temp, 1);
        return it;
    }

    auto res = insert(std::move(*temp)).first;
    temp_alloc.destroy(temp);
    temp_alloc.deallocate(temp, 1);
    return res;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_iterator pos)
{
    if (pos == end()) return end();
    auto next_it = pos;
    ++next_it;
    tkey next_key;
    bool has_next = false;
    if (next_it != end()) {
        next_key = next_it->first;
        has_next = true;
    }
    erase(pos->first);
    return has_next ? find(next_key) : end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_const_iterator pos)
{
    if (pos == cend()) return end();
    auto next_it = pos;
    ++next_it;
    tkey next_key;
    bool has_next = false;
    if (next_it != cend()) {
        next_key = next_it->first;
        has_next = true;
    }
    erase(pos->first);
    return has_next ? find(next_key) : end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_iterator beg, btree_iterator en)
{
    while (beg != en) {
        beg = erase(beg);
    }
    return en;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_const_iterator beg, btree_const_iterator en)
{
    btree_iterator it = find(beg->first);
    while (it != end() && (en == cend() || compare_keys(it->first, en->first))) {
        it = erase(it);
    }
    return en == cend() ? end() : find(en->first);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(const tkey& key)
{
    if (!_root) return end();
    auto next_it = find(key);
    if (next_it == end()) return end();
    ++next_it;
    tkey next_key;
    bool has_next = false;
    if (next_it != end()) {
        next_key = next_it->first;
        has_next = true;
    }

    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    bool found = false;
    size_t key_idx = 0;

    while (!path.empty()) {
        auto node = *path.top().first;
        size_t i = 0;
        while (i < node->_keys.size() && compare_keys(node->_keys[i].first, key)) {
            ++i;
        }
        if (i < node->_keys.size() && !compare_keys(key, node->_keys[i].first)) {
            found = true;
            key_idx = i;
            break;
        }
        if (node->_pointers.empty()) break;
        path.push({&node->_pointers[i], i});
    }

    if (!found) return end();

    auto byte_allocator = pp_allocator<btree_node>(_allocator.resource());
    auto delete_node = *path.top().first;

    if (!delete_node->_pointers.empty()) {
        std::stack<std::pair<btree_node**, size_t>> pred_path = path;
        pred_path.top().second = key_idx;
        pred_path.push({&delete_node->_pointers[key_idx], 0});
        while (!(*pred_path.top().first)->_pointers.empty()) {
            auto child = *pred_path.top().first;
            pred_path.push({&child->_pointers.back(), 0});
        }
        auto pred_node = *pred_path.top().first;
        delete_node->_keys[key_idx] = std::move(pred_node->_keys.back());
        path = pred_path;
        key_idx = pred_node->_keys.size() - 1;
    }

    auto leaf = *path.top().first;
    leaf->_keys.erase(leaf->_keys.begin() + key_idx);
    _size--;

    while (path.size() > 1) {
        auto node = *path.top().first;
        if (node->_keys.size() >= minimum_keys_in_node) break;

        auto node_pair = path.top();
        path.pop();
        auto parent = *path.top().first;
        size_t child_idx = path.top().second;

        if (child_idx > 0) {
            auto left_sibling = parent->_pointers[child_idx - 1];
            if (left_sibling->_keys.size() > minimum_keys_in_node) {
                node->_keys.insert(node->_keys.begin(), std::move(parent->_keys[child_idx - 1]));
                parent->_keys[child_idx - 1] = std::move(left_sibling->_keys.back());
                left_sibling->_keys.pop_back();
                if (!left_sibling->_pointers.empty()) {
                    node->_pointers.insert(node->_pointers.begin(), left_sibling->_pointers.back());
                    left_sibling->_pointers.pop_back();
                }
                break;
            }
        }
        if (child_idx < parent->_pointers.size() - 1) {
            auto right_sibling = parent->_pointers[child_idx + 1];
            if (right_sibling->_keys.size() > minimum_keys_in_node) {
                node->_keys.push_back(std::move(parent->_keys[child_idx]));
                parent->_keys[child_idx] = std::move(right_sibling->_keys.front());
                right_sibling->_keys.erase(right_sibling->_keys.begin());
                if (!right_sibling->_pointers.empty()) {
                    node->_pointers.push_back(right_sibling->_pointers.front());
                    right_sibling->_pointers.erase(right_sibling->_pointers.begin());
                }
                break;
            }
        }

        if (child_idx > 0) {
            auto left_sibling = parent->_pointers[child_idx - 1];
            left_sibling->_keys.push_back(std::move(parent->_keys[child_idx - 1]));
            for (auto& k : node->_keys) left_sibling->_keys.push_back(std::move(k));
            for (auto& p : node->_pointers) left_sibling->_pointers.push_back(p);
            parent->_keys.erase(parent->_keys.begin() + child_idx - 1);
            parent->_pointers.erase(parent->_pointers.begin() + child_idx);
            byte_allocator.destroy(node);
            byte_allocator.deallocate(node, 1);
            path.top().second = child_idx - 1;
        } else {
            auto right_sibling = parent->_pointers[child_idx + 1];
            node->_keys.push_back(std::move(parent->_keys[child_idx]));
            for (auto& k : right_sibling->_keys) node->_keys.push_back(std::move(k));
            for (auto& p : right_sibling->_pointers) node->_pointers.push_back(p);
            parent->_keys.erase(parent->_keys.begin() + child_idx);
            parent->_pointers.erase(parent->_pointers.begin() + child_idx + 1);
            byte_allocator.destroy(right_sibling);
            byte_allocator.deallocate(right_sibling, 1);
        }
    }

    if (_root->_keys.empty()) {
        if (!_root->_pointers.empty()) {
            auto old_root = _root;
            _root = _root->_pointers[0];
            byte_allocator.destroy(old_root);
            byte_allocator.deallocate(old_root, 1);
        } else {
            // empty tree handled gracefully
        }
    }

    return has_next ? find(next_key) : end();
}
// endregion modifiers implementation

#endif
