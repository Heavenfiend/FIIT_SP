#ifndef SYS_PROG_B_TREE_H
#define SYS_PROG_B_TREE_H

#include <algorithm>
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
#include <not_implemented.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class B_tree final : private compare
{
public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

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
    static_assert(t >= 2, "B_tree minimum degree t must be at least 2");

    static constexpr std::size_t minimum_keys_in_node = t - 1;
    static constexpr std::size_t maximum_keys_in_node = 2 * t - 1;
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    inline bool compare_keys(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }

    inline bool compare_pairs(const tree_data_type& lhs, const tree_data_type& rhs) const
    {
        return compare_keys(lhs.first, rhs.first);
    }

    bool equivalent_keys(const tkey& lhs, const tkey& rhs) const
    {
        return !compare_keys(lhs, rhs) && !compare_keys(rhs, lhs);
    }

    struct btree_node
    {
        boost::container::static_vector<tree_data_type, maximum_keys_in_node + 1> _keys;
        boost::container::static_vector<btree_node*, maximum_keys_in_node + 2> _pointers;

        btree_node() noexcept = default;
    };

    using node_allocator_type = typename std::allocator_traits<pp_allocator<value_type>>::template rebind_alloc<btree_node>;
    using node_allocator_traits = std::allocator_traits<node_allocator_type>;

    pp_allocator<value_type> _allocator;
    btree_node* _root;
    std::size_t _size;

    node_allocator_type get_node_allocator() const
    {
        return node_allocator_type(_allocator);
    }

    pp_allocator<value_type> get_allocator() const noexcept
    {
        return _allocator;
    }

    btree_node* create_node()
    {
        node_allocator_type alloc = get_node_allocator();
        btree_node* node = nullptr;

        try
        {
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
        catch (...)
        {
            if (node != nullptr)
            {
                node_allocator_traits::deallocate(alloc, node, 1);
            }
            throw;
        }
    }

    void destroy_node(btree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }

        try
        {
            node_allocator_type alloc = get_node_allocator();
            node_allocator_traits::destroy(alloc, node);
            node_allocator_traits::deallocate(alloc, node, 1);
        }
        catch (...)
        {
        }
    }

    void destroy_subtree(btree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }

        for (btree_node* child : node->_pointers)
        {
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

        btree_node* result = create_node();

        try
        {
            result->_keys = source->_keys;
            for (const btree_node* child : source->_pointers)
            {
                result->_pointers.push_back(clone_subtree(child));
            }
        }
        catch (...)
        {
            destroy_subtree(result);
            throw;
        }

        return result;
    }

    std::size_t lower_index_in_node(const btree_node* node, const tkey& key) const
    {
        std::size_t index = 0;
        while (index < node->_keys.size() && compare_keys(node->_keys[index].first, key))
        {
            ++index;
        }
        return index;
    }

    std::size_t upper_index_in_node(const btree_node* node, const tkey& key) const
    {
        std::size_t index = 0;
        while (index < node->_keys.size() && !compare_keys(key, node->_keys[index].first))
        {
            ++index;
        }
        return index;
    }

    void split_child(btree_node* parent, std::size_t child_index)
    {
        btree_node* child = parent->_pointers[child_index];
        if (child == nullptr || child->_keys.size() <= maximum_keys_in_node)
        {
            return;
        }

        btree_node* right = create_node();

        try
        {
            constexpr std::size_t median_index = t;
            tree_data_type median = std::move(child->_keys[median_index]);

            for (std::size_t index = median_index + 1; index < child->_keys.size(); ++index)
            {
                right->_keys.push_back(std::move(child->_keys[index]));
            }

            if (!child->_pointers.empty())
            {
                for (std::size_t index = median_index + 1; index < child->_pointers.size(); ++index)
                {
                    right->_pointers.push_back(child->_pointers[index]);
                }
            }

            child->_keys.resize(median_index);
            if (!child->_pointers.empty())
            {
                child->_pointers.resize(median_index + 1);
            }

            parent->_keys.insert(parent->_keys.begin() + static_cast<std::ptrdiff_t>(child_index), std::move(median));
            parent->_pointers.insert(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(child_index + 1), right);
        }
        catch (...)
        {
            destroy_node(right);
            throw;
        }
    }

    void append_in_order(const btree_node* node, std::vector<tree_data_type>& output) const
    {
        if (node == nullptr)
        {
            return;
        }

        for (std::size_t index = 0; index < node->_keys.size(); ++index)
        {
            if (!node->_pointers.empty())
            {
                append_in_order(node->_pointers[index], output);
            }
            output.push_back(node->_keys[index]);
        }

        if (!node->_pointers.empty())
        {
            append_in_order(node->_pointers[node->_keys.size()], output);
        }
    }

    void swap_with(B_tree& other) noexcept(std::is_nothrow_swappable_v<compare> && std::is_nothrow_swappable_v<pp_allocator<value_type>>)
    {
        using std::swap;
        swap(static_cast<compare&>(*this), static_cast<compare&>(other));
        swap(_allocator, other._allocator);
        swap(_root, other._root);
        swap(_size, other._size);
    }

    template <typename link_type>
    static btree_node* node_from_link(link_type link) noexcept
    {
        return link == nullptr ? nullptr : *link;
    }

    template <typename link_type>
    static btree_node* current_node(const std::stack<std::pair<link_type, std::size_t>>& path) noexcept
    {
        return path.empty() ? nullptr : node_from_link(path.top().first);
    }

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

        if (!node->_pointers.empty())
        {
            std::size_t child_index = index + 1;
            link_type child_link = &node->_pointers[child_index];
            path.push({child_link, child_index});

            while (!node_from_link(child_link)->_pointers.empty())
            {
                btree_node* child = node_from_link(child_link);
                child_link = &child->_pointers[0];
                path.push({child_link, 0});
            }

            index = 0;
            return;
        }

        if (index + 1 < node->_keys.size())
        {
            ++index;
            return;
        }

        auto original_path = path;
        while (!path.empty())
        {
            const std::size_t source_child_index = path.top().second;
            path.pop();

            if (path.empty())
            {
                path = std::move(original_path);
                btree_node* last_node = current_node(path);
                index = last_node == nullptr ? 0 : last_node->_keys.size();
                return;
            }

            btree_node* parent = current_node(path);
            if (source_child_index < parent->_keys.size())
            {
                index = source_child_index;
                return;
            }
        }
    }

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
            std::size_t child_index = index;
            link_type child_link = &node->_pointers[child_index];
            path.push({child_link, child_index});

            while (!node_from_link(child_link)->_pointers.empty())
            {
                btree_node* child = node_from_link(child_link);
                child_index = child->_pointers.size() - 1;
                child_link = &child->_pointers[child_index];
                path.push({child_link, child_index});
            }

            btree_node* child = node_from_link(child_link);
            index = child->_keys.empty() ? 0 : child->_keys.size() - 1;
            return;
        }

        if (index > 0)
        {
            --index;
            return;
        }

        auto original_path = path;
        while (!path.empty())
        {
            const std::size_t source_child_index = path.top().second;
            path.pop();

            if (path.empty())
            {
                path = std::move(original_path);
                index = npos;
                return;
            }

            if (source_child_index > 0)
            {
                index = source_child_index - 1;
                return;
            }
        }
    }

public:
    class btree_iterator;
    class btree_reverse_iterator;
    class btree_const_iterator;
    class btree_const_reverse_iterator;

    class btree_iterator final
    {
        std::stack<std::pair<btree_node**, std::size_t>> _path;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = tree_data_type&;
        using pointer = tree_data_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = btree_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;

        explicit btree_iterator(const std::stack<std::pair<btree_node**, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            move_to_next(_path, _index);
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            move_to_previous(_path, _index);
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }

        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }

        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }

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

        explicit btree_const_iterator(const std::stack<std::pair<btree_node* const*, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        btree_const_iterator(const btree_iterator& it) noexcept : _index(it._index)
        {
            std::vector<std::pair<btree_node* const*, std::size_t>> reversed;
            auto path = it._path;
            while (!path.empty())
            {
                reversed.push_back({path.top().first, path.top().second});
                path.pop();
            }
            for (auto iter = reversed.rbegin(); iter != reversed.rend(); ++iter)
            {
                _path.push(*iter);
            }
        }

        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            move_to_next(_path, _index);
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            move_to_previous(_path, _index);
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }

        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }

        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }

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

        explicit btree_reverse_iterator(const std::stack<std::pair<btree_node**, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        btree_reverse_iterator(const btree_iterator& it) noexcept : _path(it._path), _index(it._index)
        {
        }

        operator btree_iterator() const noexcept
        {
            return btree_iterator(_path, _index);
        }

        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            move_to_previous(_path, _index);
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            move_to_next(_path, _index);
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }

        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }

        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }

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

        explicit btree_const_reverse_iterator(const std::stack<std::pair<btree_node* const*, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        btree_const_reverse_iterator(const btree_reverse_iterator& it) noexcept : _index(it._index)
        {
            std::vector<std::pair<btree_node* const*, std::size_t>> reversed;
            auto path = it._path;
            while (!path.empty())
            {
                reversed.push_back({path.top().first, path.top().second});
                path.pop();
            }
            for (auto iter = reversed.rbegin(); iter != reversed.rend(); ++iter)
            {
                _path.push(*iter);
            }
        }

        operator btree_const_iterator() const noexcept
        {
            return btree_const_iterator(_path, _index);
        }

        reference operator*() const noexcept
        {
            btree_node* node = current_node(_path);
            return node->_keys[_index];
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            move_to_previous(_path, _index);
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            move_to_next(_path, _index);
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0 : _path.size() - 1;
        }

        std::size_t current_node_keys_count() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr ? 0 : node->_keys.size();
        }

        bool is_terminate_node() const noexcept
        {
            btree_node* node = current_node(_path);
            return node == nullptr || node->_pointers.empty();
        }

        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    explicit B_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(std::move(alloc)), _root(nullptr), _size(0)
    {
    }

    explicit B_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : B_tree(comp, std::move(alloc))
    {
    }

    template <input_iterator_for_pair<tkey, tvalue> iterator>
    explicit B_tree(iterator first, iterator last, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : B_tree(cmp, std::move(alloc))
    {
        try
        {
            for (; first != last; ++first)
            {
                insert(*first);
            }
        }
        catch (...)
        {
            clear();
            throw;
        }
    }

    B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : B_tree(data.begin(), data.end(), cmp, std::move(alloc))
    {
    }

    B_tree(const B_tree& other)
        : compare(static_cast<const compare&>(other)), _allocator(other._allocator), _root(nullptr), _size(0)
    {
        _root = clone_subtree(other._root);
        _size = other._size;
    }

    B_tree(B_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(std::move(other._allocator)),
          _root(other._root),
          _size(other._size)
    {
        other._root = nullptr;
        other._size = 0;
    }

    B_tree& operator=(const B_tree& other)
    {
        if (this != &other)
        {
            B_tree copy(other);
            swap_with(copy);
        }
        return *this;
    }

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

    ~B_tree() noexcept
    {
        clear();
    }

    tvalue& at(const tkey& key)
    {
        auto iter = find(key);
        if (iter == end())
        {
            throw key_not_found();
        }
        return iter->second;
    }

    const tvalue& at(const tkey& key) const
    {
        auto iter = find(key);
        if (iter == end())
        {
            throw key_not_found();
        }
        return iter->second;
    }

    tvalue& operator[](const tkey& key)
    {
        return insert(tree_data_type(key, tvalue{})).first->second;
    }

    tvalue& operator[](tkey&& key)
    {
        return insert(tree_data_type(std::move(key), tvalue{})).first->second;
    }

    btree_iterator begin()
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_iterator(path, 0);
        }

        btree_node** link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            link = &(*link)->_pointers[0];
            path.push({link, 0});
        }
        return btree_iterator(path, 0);
    }

    btree_iterator end()
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return btree_iterator(path, 0);
        }

        btree_node** link = &_root;
        path.push({link, 0});
        while (!(*link)->_pointers.empty())
        {
            std::size_t child_index = (*link)->_pointers.size() - 1;
            link = &(*link)->_pointers[child_index];
            path.push({link, child_index});
        }
        return btree_iterator(path, (*link)->_keys.size());
    }

    btree_const_iterator begin() const
    {
        return cbegin();
    }

    btree_const_iterator end() const
    {
        return cend();
    }

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

    btree_const_reverse_iterator rbegin() const
    {
        return crbegin();
    }

    btree_const_reverse_iterator rend() const
    {
        return crend();
    }

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

    std::size_t size() const noexcept
    {
        return _size;
    }

    bool empty() const noexcept
    {
        return _size == 0;
    }

    btree_iterator find(const tkey& key)
    {
        auto iter = lower_bound(key);
        if (iter == end() || !equivalent_keys(iter->first, key))
        {
            return end();
        }
        return iter;
    }

    btree_const_iterator find(const tkey& key) const
    {
        auto iter = lower_bound(key);
        if (iter == end() || !equivalent_keys(iter->first, key))
        {
            return end();
        }
        return iter;
    }

    btree_iterator lower_bound(const tkey& key)
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        std::stack<std::pair<btree_node**, std::size_t>> candidate_path;
        std::size_t candidate_index = 0;
        bool candidate_exists = false;

        btree_node** link = &_root;
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

        return candidate_exists ? btree_iterator(candidate_path, candidate_index) : end();
    }

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

    btree_iterator upper_bound(const tkey& key)
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        std::stack<std::pair<btree_node**, std::size_t>> candidate_path;
        std::size_t candidate_index = 0;
        bool candidate_exists = false;

        btree_node** link = &_root;
        std::size_t source_child_index = 0;

        while (*link != nullptr)
        {
            path.push({link, source_child_index});
            btree_node* node = *link;
            const std::size_t index = upper_index_in_node(node, key);

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

        return candidate_exists ? btree_iterator(candidate_path, candidate_index) : end();
    }

    btree_const_iterator upper_bound(const tkey& key) const
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
            const std::size_t index = upper_index_in_node(node, key);

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

    bool contains(const tkey& key) const
    {
        return find(key) != end();
    }

    void clear() noexcept
    {
        destroy_subtree(_root);
        _root = nullptr;
        _size = 0;
    }

private:
    bool remove_from_node(btree_node* node, const tkey& key)
{
    if (node == nullptr)
    {
        return false;
    }

    const std::size_t index = lower_index_in_node(node, key);

    if (index < node->_keys.size() && equivalent_keys(node->_keys[index].first, key))
    {
        if (node->_pointers.empty())
        {
            node->_keys.erase(node->_keys.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }

        return remove_from_internal_node(node, index);
    }

    if (node->_pointers.empty())
    {
        return false;
    }

    const bool was_last_child = index == node->_keys.size();

    if (node->_pointers[index]->_keys.size() == minimum_keys_in_node)
    {
        fill_child_before_descent(node, index);
    }

    if (was_last_child && index > node->_keys.size())
    {
        return remove_from_node(node->_pointers[index - 1], key);
    }

    return remove_from_node(node->_pointers[index], key);
}

bool remove_from_internal_node(btree_node* node, std::size_t index)
{
    const tkey key = node->_keys[index].first;

    btree_node* left_child = node->_pointers[index];
    btree_node* right_child = node->_pointers[index + 1];

    if (left_child->_keys.size() >= t)
    {
        tree_data_type predecessor = get_predecessor(node, index);
        const tkey predecessor_key = predecessor.first;

        node->_keys[index] = std::move(predecessor);

        return remove_from_node(left_child, predecessor_key);
    }

    if (right_child->_keys.size() >= t)
    {
        tree_data_type successor = get_successor(node, index);
        const tkey successor_key = successor.first;

        node->_keys[index] = std::move(successor);

        return remove_from_node(right_child, successor_key);
    }

    merge_children(node, index);

    return remove_from_node(left_child, key);
}

tree_data_type get_predecessor(btree_node* node, std::size_t index) const
{
    btree_node* current = node->_pointers[index];

    while (!current->_pointers.empty())
    {
        current = current->_pointers.back();
    }

    return current->_keys.back();
}

tree_data_type get_successor(btree_node* node, std::size_t index) const
{
    btree_node* current = node->_pointers[index + 1];

    while (!current->_pointers.empty())
    {
        current = current->_pointers.front();
    }

    return current->_keys.front();
}

void fill_child_before_descent(btree_node* parent, std::size_t child_index)
{
    if (child_index > 0 && parent->_pointers[child_index - 1]->_keys.size() >= t)
    {
        borrow_from_previous(parent, child_index);
        return;
    }

    if (child_index + 1 < parent->_pointers.size() &&
        parent->_pointers[child_index + 1]->_keys.size() >= t)
    {
        borrow_from_next(parent, child_index);
        return;
    }

    if (child_index + 1 < parent->_pointers.size())
    {
        merge_children(parent, child_index);
    }
    else
    {
        merge_children(parent, child_index - 1);
    }
}

void borrow_from_previous(btree_node* parent, std::size_t child_index)
{
    btree_node* child = parent->_pointers[child_index];
    btree_node* sibling = parent->_pointers[child_index - 1];

    child->_keys.insert(child->_keys.begin(), std::move(parent->_keys[child_index - 1]));

    if (!sibling->_pointers.empty())
    {
        child->_pointers.insert(child->_pointers.begin(), sibling->_pointers.back());
        sibling->_pointers.pop_back();
    }

    parent->_keys[child_index - 1] = std::move(sibling->_keys.back());
    sibling->_keys.pop_back();
}

void borrow_from_next(btree_node* parent, std::size_t child_index)
{
    btree_node* child = parent->_pointers[child_index];
    btree_node* sibling = parent->_pointers[child_index + 1];

    child->_keys.push_back(std::move(parent->_keys[child_index]));

    if (!sibling->_pointers.empty())
    {
        child->_pointers.push_back(sibling->_pointers.front());
        sibling->_pointers.erase(sibling->_pointers.begin());
    }

    parent->_keys[child_index] = std::move(sibling->_keys.front());
    sibling->_keys.erase(sibling->_keys.begin());
}

void merge_children(btree_node* parent, std::size_t left_child_index)
{
    btree_node* left_child = parent->_pointers[left_child_index];
    btree_node* right_child = parent->_pointers[left_child_index + 1];

    left_child->_keys.push_back(std::move(parent->_keys[left_child_index]));

    for (auto& key_value : right_child->_keys)
    {
        left_child->_keys.push_back(std::move(key_value));
    }

    for (btree_node* child : right_child->_pointers)
    {
        left_child->_pointers.push_back(child);
    }

    parent->_keys.erase(parent->_keys.begin() + static_cast<std::ptrdiff_t>(left_child_index));
    parent->_pointers.erase(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1));

    right_child->_pointers.clear();
    destroy_node(right_child);
}

    void insert_into_subtree(btree_node* node, tree_data_type data)
    {
        const tkey& key = data.first;
        std::size_t index = lower_index_in_node(node, key);

        if (node->_pointers.empty())
        {
            node->_keys.insert(node->_keys.begin() + static_cast<std::ptrdiff_t>(index), std::move(data));
            return;
        }

        insert_into_subtree(node->_pointers[index], std::move(data));

        if (node->_pointers[index]->_keys.size() > maximum_keys_in_node)
        {
            split_child(node, index);
        }
    }

    std::pair<btree_iterator, bool> insert_value(tree_data_type data)
    {
        const tkey search_key = data.first;
        auto existing = find(search_key);
        if (existing != end())
        {
            return {existing, false};
        }

        if (_root == nullptr)
        {
            _root = create_node();
            try
            {
                _root->_keys.push_back(std::move(data));
                _size = 1;
                return {find(search_key), true};
            }
            catch (...)
            {
                destroy_node(_root);
                _root = nullptr;
                _size = 0;
                throw;
            }
        }

        insert_into_subtree(_root, std::move(data));

        if (_root->_keys.size() > maximum_keys_in_node)
        {
            btree_node* new_root = create_node();
            try
            {
                new_root->_pointers.push_back(_root);
                split_child(new_root, 0);
                _root = new_root;
            }
            catch (...)
            {
                destroy_node(new_root);
                throw;
            }
        }

        ++_size;
        return {find(search_key), true};
    }

    bool key_is_in_vector(const tkey& key, const std::vector<tkey>& keys) const
    {
        return std::any_of(keys.begin(), keys.end(), [this, &key](const tkey& candidate) {
            return equivalent_keys(key, candidate);
        });
    }

public:
    std::pair<btree_iterator, bool> insert(const tree_data_type& data)
    {
        return insert_value(data);
    }

    std::pair<btree_iterator, bool> insert(tree_data_type&& data)
    {
        return insert_value(std::move(data));
    }

    template <typename... Args>
    std::pair<btree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        return insert(std::move(data));
    }

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

    template <typename... Args>
    btree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        return insert_or_assign(std::move(data));
    }

    btree_iterator erase(btree_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }
        const tkey key = pos->first;
        return erase(key);
    }

    btree_iterator erase(btree_const_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }
        const tkey key = pos->first;
        return erase(key);
    }

    btree_iterator erase(btree_iterator first, btree_iterator last)
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
        auto iter = find(key);
        if (iter == end())
        {
            return end();
        }

        std::optional<tkey> return_key;

        auto next = iter;
        ++next;

        if (next != end())
        {
            return_key = next->first;
        }

        if (remove_from_node(_root, key))
        {
            --_size;
        }

        if (_root != nullptr && _root->_keys.empty())
        {
            if (_root->_pointers.empty())
            {
                destroy_node(_root);
                _root = nullptr;
            }
            else
            {
                btree_node* old_root = _root;
                _root = old_root->_pointers.front();
                old_root->_pointers.clear();
                destroy_node(old_root);
            }
        }

        return return_key.has_value() ? lower_bound(*return_key) : end();
    }
};

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

template <
    typename tkey,
    typename tvalue,
    comparator<tkey> compare = std::less<tkey>,
    std::size_t t = 5,
    typename U = std::pair<const tkey, tvalue>>
B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<tkey, tvalue, compare, t>;

template <typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool compare_pairs(const typename B_tree<tkey, tvalue, compare, t>::tree_data_type& lhs,
                   const typename B_tree<tkey, tvalue, compare, t>::tree_data_type& rhs)
{
    return compare{}(lhs.first, rhs.first);
}

template <typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool compare_keys(const tkey& lhs, const tkey& rhs)
{
    return compare{}(lhs, rhs);
}

#endif
