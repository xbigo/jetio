#pragma once

// A vector over memory it does not allocate: the elements [0, size) of a buffer with room for
// `capacity` elements. It gives the interface of std::vector, without growing, and takes care of
// the construction and destruction of the elements, which is the hard part on raw memory.
//
// The buffer is described by a represent, a triple of
//   ptr       T *
//   size      std::size_t
//   capacity  std::size_t
// either as the data members of that name or as the accessors ptr(), size() and capacity().
// buffer_represent is the plain triple. The vector reads the triple once, when it is made, and
// works on a copy of its own: it writes nothing back. Whoever uses the vector does, with the size
// that it reports or that release() returns.
//
// The vector owns the elements, not the memory: it destroys the elements in its destructor,
// unless release() gave them up, which leaves it empty and without capacity.
// See docs/design/fixed-vector.md.

#include <jio/std/contract.hpp>

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace jio
{
    /// The plain represent: `size` elements at `ptr`, in memory for `capacity` elements.
    template <typename T> struct buffer_represent
    {
        T *ptr = nullptr;
        std::size_t size = 0;
        std::size_t capacity = 0;
    };

    namespace detail
    {
        template <typename Rep, typename T>
        concept member_fixed_represent = requires(const Rep &rep) {
            { rep.ptr } -> std::convertible_to<T *>;
            { rep.size } -> std::convertible_to<std::size_t>;
            { rep.capacity } -> std::convertible_to<std::size_t>;
        };

        template <typename Rep, typename T>
        concept accessor_fixed_represent = requires(const Rep &rep) {
            { rep.ptr() } -> std::convertible_to<T *>;
            { rep.size() } -> std::convertible_to<std::size_t>;
            { rep.capacity() } -> std::convertible_to<std::size_t>;
        };
    } // namespace detail

    /// A represent of elements of type T, see above. The vector only reads it.
    template <typename Rep, typename T>
    concept fixed_represent =
        detail::member_fixed_represent<Rep, T> || detail::accessor_fixed_represent<Rep, T>;

    template <typename T> class fixed_vector
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T &;
        using const_reference = const T &;
        using pointer = T *;
        using const_pointer = const T *;
        using iterator = T *;
        using const_iterator = const T *;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        /// Takes over the elements in the buffer that the represent describes: its `size` elements
        /// are constructed already, and become the vector's.
        /// @pre size <= capacity && (capacity == 0 || ptr != nullptr) (critical)
        template <fixed_represent<T> Represent>
        explicit fixed_vector(const Represent &rep)
            : m_ptr(read_ptr_(rep)), m_size(read_size_(rep)), m_capacity(read_capacity_(rep))
        {
            JIO_Expects_critical(m_size <= m_capacity && (m_capacity == 0 || m_ptr != nullptr));
        }

        fixed_vector(const fixed_vector &) = delete;
        fixed_vector &operator=(const fixed_vector &) = delete;

        /// Takes the elements over: `other` is empty and without capacity afterwards.
        fixed_vector(fixed_vector &&other) noexcept
            : m_ptr(other.m_ptr), m_size(other.m_size), m_capacity(other.m_capacity)
        {
            other.detach_();
        }

        fixed_vector &operator=(fixed_vector &&other) noexcept
        {
            if (this != &other)
            {
                clear();
                m_ptr = other.m_ptr;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                other.detach_();
            }
            return *this;
        }

        ~fixed_vector() { clear(); }

        /// Gives the elements up: the caller destroys them, and finds them in the returned
        /// represent, which says how many there are. The vector is empty afterwards and without
        /// capacity, so it cannot overwrite what it gave up.
        buffer_represent<T> release() noexcept
        {
            const buffer_represent<T> given{m_ptr, m_size, m_capacity};
            detach_();
            return given;
        }

        // --- capacity ---------------------------------------------------------------------
        [[nodiscard]] size_type size() const noexcept { return m_size; }
        [[nodiscard]] size_type capacity() const noexcept { return m_capacity; }
        [[nodiscard]] size_type max_size() const noexcept { return m_capacity; }
        [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
        [[nodiscard]] bool full() const noexcept { return m_size == m_capacity; }

        // --- element access ---------------------------------------------------------------
        [[nodiscard]] pointer data() noexcept { return m_ptr; }
        [[nodiscard]] const_pointer data() const noexcept { return m_ptr; }

        /// @pre n < size() (critical)
        [[nodiscard]] reference operator[](size_type n) noexcept
        {
            JIO_Expects_critical(n < m_size);
            return m_ptr[n];
        }
        /// @pre n < size() (critical)
        [[nodiscard]] const_reference operator[](size_type n) const noexcept
        {
            JIO_Expects_critical(n < m_size);
            return m_ptr[n];
        }

        /// @pre !empty() (critical)
        [[nodiscard]] reference front() noexcept { return (*this)[0]; }
        /// @pre !empty() (critical)
        [[nodiscard]] const_reference front() const noexcept { return (*this)[0]; }
        /// @pre !empty() (critical)
        [[nodiscard]] reference back() noexcept { return (*this)[m_size - 1]; }
        /// @pre !empty() (critical)
        [[nodiscard]] const_reference back() const noexcept { return (*this)[m_size - 1]; }

        // --- iterators --------------------------------------------------------------------
        [[nodiscard]] iterator begin() noexcept { return m_ptr; }
        [[nodiscard]] const_iterator begin() const noexcept { return m_ptr; }
        [[nodiscard]] const_iterator cbegin() const noexcept { return m_ptr; }
        [[nodiscard]] iterator end() noexcept { return m_ptr + m_size; }
        [[nodiscard]] const_iterator end() const noexcept { return m_ptr + m_size; }
        [[nodiscard]] const_iterator cend() const noexcept { return m_ptr + m_size; }
        [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        [[nodiscard]] const_reverse_iterator rbegin() const noexcept
        {
            return const_reverse_iterator(end());
        }
        [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return rbegin(); }
        [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        [[nodiscard]] const_reverse_iterator rend() const noexcept
        {
            return const_reverse_iterator(begin());
        }
        [[nodiscard]] const_reverse_iterator crend() const noexcept { return rend(); }

        // --- modifiers --------------------------------------------------------------------
        // An operation that throws (the constructor of an element) leaves the vector as it was.

        /// Constructs an element at the end. The element counts only if its construction
        /// succeeds, which makes this the one place that decides how an append fails.
        /// @pre !full() (critical)
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        reference emplace_back(Args &&...args)
        {
            JIO_Expects_critical(m_size < m_capacity);
            return construct_back_(std::forward<Args>(args)...);
        }

        /// @return the new element, or nullptr if the vector is full
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        pointer try_emplace_back(Args &&...args)
        {
            if (full())
                return nullptr;
            return &construct_back_(std::forward<Args>(args)...);
        }

        /// @pre !full() (critical)
        reference push_back(const T &value) { return emplace_back(value); }
        /// @pre !full() (critical)
        reference push_back(T &&value) { return emplace_back(std::move(value)); }
        /// @return the new element, or nullptr if the vector is full
        pointer try_push_back(const T &value) { return try_emplace_back(value); }
        /// @return the new element, or nullptr if the vector is full
        pointer try_push_back(T &&value) { return try_emplace_back(std::move(value)); }

        /// Destroys the last element.
        /// @pre !empty() (critical)
        void pop_back() noexcept
        {
            JIO_Expects_critical(m_size > 0);
            std::destroy_at(m_ptr + m_size - 1);
            --m_size;
        }

        /// Destroys all the elements.
        void clear() noexcept
        {
            std::destroy(begin(), end());
            m_size = 0;
        }

        /// Makes the vector n elements long: destroys the ones beyond, or adds value initialized
        /// ones.
        /// @pre n <= capacity() (critical)
        void resize(size_type n) { resize_(n); }
        /// Like resize(n), adding copies of `value`.
        /// @pre n <= capacity() (critical)
        void resize(size_type n, const T &value) { resize_(n, value); }

        /// Removes the element at `pos`. Basic exception safety: the elements stay valid, they
        /// may have been moved from if a move throws.
        /// @pre begin() <= pos && pos < end() (critical)
        /// @return the element after the removed one
        iterator erase(const_iterator pos) { return erase(pos, pos + 1); }

        /// Removes the elements in [first, last), see erase(pos).
        /// @pre begin() <= first && first <= last && last <= end() (critical)
        iterator erase(const_iterator first, const_iterator last)
        {
            JIO_Expects_critical(begin() <= first && first <= last && last <= end());
            const iterator from = begin() + (first - cbegin());
            const iterator to = begin() + (last - cbegin());
            if (from != to)
            {
                const iterator new_end = std::move(to, end(), from);
                std::destroy(new_end, end());
                m_size = static_cast<size_type>(new_end - begin());
            }
            return from;
        }

        /// The element-wise comparison of the vectors.
        friend bool operator==(const fixed_vector &a, const fixed_vector &b)
            requires std::equality_comparable<T>
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end());
        }

        friend auto operator<=>(const fixed_vector &a, const fixed_vector &b)
            requires std::three_way_comparable<T>
        {
            return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
        }

    private:
        // The one place that knows the two forms of a represent.
        template <typename Represent> static T *read_ptr_(const Represent &rep)
        {
            if constexpr (detail::accessor_fixed_represent<Represent, T>)
                return rep.ptr();
            else
                return rep.ptr;
        }

        template <typename Represent> static size_type read_size_(const Represent &rep)
        {
            if constexpr (detail::accessor_fixed_represent<Represent, T>)
                return rep.size();
            else
                return rep.size;
        }

        template <typename Represent> static size_type read_capacity_(const Represent &rep)
        {
            if constexpr (detail::accessor_fixed_represent<Represent, T>)
                return rep.capacity();
            else
                return rep.capacity;
        }

        /// Empty and without capacity: it owns and destroys nothing, and cannot add anything.
        void detach_() noexcept
        {
            m_size = 0;
            m_capacity = 0;
        }

        /// Constructs the element after the last, which counts only if the construction succeeds.
        template <typename... Args> reference construct_back_(Args &&...args)
        {
            T *element = std::construct_at(m_ptr + m_size, std::forward<Args>(args)...);
            ++m_size;
            return *element;
        }

        /// Makes the size n; the elements added are made of `args`. They go to a vector over the
        /// free part of the buffer, which destroys them again if one cannot be made, and gives
        /// them up when all are there. This function, not that vector, then sets the size.
        template <typename... Args> void resize_(size_type n, const Args &...args)
        {
            JIO_Expects_critical(n <= m_capacity);
            const size_type old_size = m_size;
            if (n <= old_size)
            {
                std::destroy(m_ptr + n, m_ptr + old_size);
                m_size = n;
                return;
            }
            fixed_vector added(buffer_represent<T>{m_ptr + old_size, 0, m_capacity - old_size});
            while (old_size + added.size() < n)
                added.emplace_back(args...); // not forwarded: the same arguments serve each element
            m_size = n;
            added.release(); // the elements are ours now
        }

        T *m_ptr = nullptr;
        size_type m_size = 0;
        size_type m_capacity = 0;
    };

    template <typename T> fixed_vector(buffer_represent<T>) -> fixed_vector<T>;
} // namespace jio
