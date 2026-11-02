#include <jetio/std/fixed_vector.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace jetio;

namespace
{
    // Raw memory for n elements: no element is constructed in it.
    template <typename T> class raw_memory
    {
    public:
        explicit raw_memory(std::size_t n) : m_ptr(std::allocator<T>().allocate(n)), m_size(n) {}
        raw_memory(const raw_memory &) = delete;
        raw_memory &operator=(const raw_memory &) = delete;
        ~raw_memory() { std::allocator<T>().deallocate(m_ptr, m_size); }
        T *get() const { return m_ptr; }
        std::size_t capacity() const { return m_size; }

    private:
        T *m_ptr;
        std::size_t m_size;
    };

    template <typename T> buffer_represent<T> empty_in(const raw_memory<T> &memory)
    {
        return {memory.get(), 0, memory.capacity()};
    }

    // Counts the elements alive, and fails to construct after a number of constructions.
    struct tracked
    {
        static inline int alive = 0;
        static inline int fail_in = -1; // the construction that throws, counted from 0

        static void reset()
        {
            alive = 0;
            fail_in = -1;
        }
        static void on_construct()
        {
            if (fail_in == 0)
                throw std::runtime_error("construction failed");
            if (fail_in > 0)
                --fail_in;
            ++alive;
        }

        int value = 0;

        explicit tracked(int v = 0) : value(v) { on_construct(); }
        tracked(const tracked &other) : value(other.value) { on_construct(); }
        tracked(tracked &&other) noexcept : value(other.value)
        {
            ++alive;
            other.value = -1;
        }
        tracked &operator=(const tracked &) = default;
        tracked &operator=(tracked &&other) noexcept
        {
            value = other.value;
            other.value = -1;
            return *this;
        }
        ~tracked() { --alive; }
    };

    // The same three things as accessors: the size is in a counter that the program keeps.
    struct counted_represent
    {
        std::string *memory;
        std::size_t room;
        const std::size_t *counter;

        std::string *ptr() const noexcept { return memory; }
        std::size_t size() const noexcept { return *counter; }
        std::size_t capacity() const noexcept { return room; }
    };

    struct without_capacity
    {
        std::string *ptr() const noexcept;
        std::size_t size() const noexcept;
    };
} // namespace

TEST_CASE("a represent says where the elements are", "[fixed_vector][concepts]")
{
    static_assert(fixed_represent<buffer_represent<std::string>, std::string>);
    static_assert(fixed_represent<counted_represent, std::string>);
    static_assert(!fixed_represent<without_capacity, std::string>);
    static_assert(!fixed_represent<buffer_represent<int>, std::string>);
}

TEST_CASE("a fixed_vector of strings works like a vector without growing", "[fixed_vector]")
{
    raw_memory<std::string> memory(4);
    fixed_vector<std::string> v(empty_in(memory));

    CHECK(v.empty());
    CHECK(v.capacity() == 4);
    CHECK(v.max_size() == 4);
    CHECK(v.data() == memory.get());

    v.push_back("one");
    v.emplace_back(3, 't');
    std::string three = "three";
    v.push_back(std::move(three));
    CHECK(v.size() == 3);
    CHECK(!v.full());
    CHECK(v[0] == "one");
    CHECK(v[1] == "ttt");
    CHECK(v.front() == "one");
    CHECK(v.back() == "three");

    std::string joined;
    for (const std::string &s : v)
        joined += s + ",";
    CHECK(joined == "one,ttt,three,");
    CHECK(*v.rbegin() == "three");

    CHECK(v.try_push_back("four") != nullptr);
    CHECK(v.full());
    CHECK(v.try_push_back("five") == nullptr);
    CHECK(v.try_emplace_back(2, 'x') == nullptr);
    CHECK(v.size() == 4);

    v.pop_back();
    CHECK(v.size() == 3);
    CHECK(v.back() == "three");
    v.clear();
    CHECK(v.empty());
}

TEST_CASE("a fixed_vector without memory is empty and full", "[fixed_vector]")
{
    fixed_vector<std::string> v(buffer_represent<std::string>{});
    CHECK(v.empty());
    CHECK(v.full());
    CHECK(v.begin() == v.end());
    CHECK(v.try_push_back("x") == nullptr);
}

TEST_CASE("the destructor destroys the elements", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> memory(4);
    {
        fixed_vector<tracked> v(empty_in(memory));
        v.emplace_back(1);
        v.emplace_back(2);
        CHECK(tracked::alive == 2);
    }
    CHECK(tracked::alive == 0);
}

TEST_CASE("release gives the elements up", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> memory(4);
    buffer_represent<tracked> released;
    {
        fixed_vector<tracked> v(empty_in(memory));
        v.emplace_back(1);
        v.emplace_back(2);
        released = v.release();
        CHECK(v.empty());
        CHECK(v.capacity() == 0); // it cannot overwrite what it gave up
        CHECK(v.full());
    }
    CHECK(tracked::alive == 2); // the vector did not destroy them
    CHECK(released.ptr == memory.get());
    CHECK(released.size == 2);
    CHECK(released.capacity == 4);
    CHECK(released.ptr[1].value == 2);

    // the vector can take them over again
    {
        fixed_vector<tracked> again(released);
        CHECK(again.size() == 2);
    }
    CHECK(tracked::alive == 0);
}

namespace
{
    // Builds n elements in the buffer of the caller. On success the caller gets them, on a failure
    // none is left behind, and no try and catch is needed.
    std::size_t build(buffer_represent<tracked> buffer, std::size_t n)
    {
        buffer.size = 0;
        fixed_vector<tracked> v(buffer);
        for (std::size_t i = 0; i < n; ++i)
            v.emplace_back(static_cast<int>(i));
        return v.release().size;
    }
} // namespace

TEST_CASE("a function that fills a buffer leaves nothing behind when it fails", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> memory(8);
    const buffer_represent<tracked> buffer{memory.get(), 0, memory.capacity()};

    CHECK(build(buffer, 5) == 5);
    CHECK(tracked::alive == 5);
    std::destroy_n(memory.get(), 5);
    CHECK(tracked::alive == 0);

    tracked::fail_in = 3; // the fourth construction throws
    CHECK_THROWS_AS(build(buffer, 5), std::runtime_error);
    CHECK(tracked::alive == 0);
}

TEST_CASE("an element that cannot be constructed leaves the vector as it was", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> memory(4);
    fixed_vector<tracked> v(empty_in(memory));
    v.emplace_back(1);

    tracked::fail_in = 0;
    CHECK_THROWS_AS(v.emplace_back(2), std::runtime_error);
    CHECK(v.size() == 1);
    CHECK(tracked::alive == 1);

    tracked::fail_in = -1;
    v.emplace_back(3);
    CHECK(v.size() == 2);
    CHECK(v.back().value == 3);
}

TEST_CASE("resize adds or removes elements", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> memory(6);
    fixed_vector<tracked> v(empty_in(memory));

    v.resize(3, tracked(7));
    CHECK(v.size() == 3);
    CHECK(v[2].value == 7);
    v.resize(5);
    CHECK(v.size() == 5);
    CHECK(v[4].value == 0);
    v.resize(2);
    CHECK(v.size() == 2);
    CHECK(tracked::alive == 2);
}

TEST_CASE("a resize that fails destroys what it made", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> memory(6);
    fixed_vector<tracked> v(empty_in(memory));
    v.emplace_back(1);

    tracked::fail_in = 2; // two are made, the third throws
    CHECK_THROWS_AS(v.resize(5), std::runtime_error);
    CHECK(v.size() == 1);
    CHECK(tracked::alive == 1);
}

TEST_CASE("erase removes elements and keeps the order", "[fixed_vector]")
{
    raw_memory<std::string> memory(6);
    fixed_vector<std::string> v(empty_in(memory));
    for (const char *s : {"a", "b", "c", "d", "e"})
        v.push_back(s);

    auto next = v.erase(v.begin() + 1); // b
    CHECK(*next == "c");
    CHECK(v.size() == 4);
    CHECK(v[1] == "c");

    next = v.erase(v.begin() + 1, v.begin() + 3); // c, d
    CHECK(*next == "e");
    CHECK(v.size() == 2);
    CHECK(v.back() == "e");

    CHECK(v.erase(v.begin(), v.begin()) == v.begin()); // nothing
    CHECK(v.size() == 2);
}

TEST_CASE("moving a fixed_vector transfers the ownership", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> first(4);
    raw_memory<tracked> second(4);
    {
        fixed_vector<tracked> a(empty_in(first));
        a.emplace_back(1);
        a.emplace_back(2);

        fixed_vector<tracked> b(std::move(a));
        // NOLINTBEGIN(bugprone-use-after-move) a moved-from vector is empty, by design
        CHECK(a.empty());
        CHECK(a.capacity() == 0);
        // NOLINTEND(bugprone-use-after-move)
        CHECK(b.size() == 2);
        CHECK(tracked::alive == 2);

        fixed_vector<tracked> c(empty_in(second));
        c.emplace_back(9);
        CHECK(tracked::alive == 3);
        c = std::move(b); // the element of c is destroyed, those of b are taken
        CHECK(tracked::alive == 2);
        CHECK(c.size() == 2);
        CHECK(c.data() == first.get());
    }
    CHECK(tracked::alive == 0); // each element destroyed once
}

TEST_CASE("elements that are in the represent already are adopted", "[fixed_vector]")
{
    tracked::reset();
    raw_memory<tracked> memory(4);
    std::construct_at(memory.get(), 5);
    std::construct_at(memory.get() + 1, 6);
    {
        fixed_vector<tracked> v(buffer_represent<tracked>{memory.get(), 2, memory.capacity()});
        CHECK(v.size() == 2);
        CHECK(v[1].value == 6);
    }
    CHECK(tracked::alive == 0);
}

TEST_CASE("a represent with accessors is adopted, and the program writes the size back",
          "[fixed_vector]")
{
    raw_memory<std::string> memory(3);
    std::construct_at(memory.get(), "a");
    std::size_t counter = 1;

    fixed_vector<std::string> v(counted_represent{memory.get(), memory.capacity(), &counter});
    CHECK(v.size() == 1);
    CHECK(v[0] == "a");
    CHECK(v.capacity() == 3);

    v.push_back("b");
    v.resize(3, "c");
    CHECK(counter == 1); // the vector does not write to the represent, whoever uses it does
    counter = v.size();
    CHECK(counter == 3);
}
TEST_CASE("release says how many elements the caller now owns", "[fixed_vector]")
{
    raw_memory<std::string> memory(3);
    const std::size_t counter_before = 0;
    std::size_t counter = counter_before;
    {
        fixed_vector<std::string> v(counted_represent{memory.get(), memory.capacity(), &counter});
        v.push_back("x");
        v.push_back("y");

        const buffer_represent<std::string> given = v.release();
        CHECK(given.ptr == memory.get());
        CHECK(given.size == 2);
        CHECK(given.capacity == 3);
        CHECK(v.capacity() == 0);
        CHECK(v.empty());
        counter = given.size; // the program keeps its own counter
    }
    CHECK(counter == 2); // the vector did not destroy them or touch the counter
    std::destroy_n(memory.get(), counter);
}
TEST_CASE("fixed_vectors compare like vectors", "[fixed_vector]")
{
    raw_memory<std::string> m1(3);
    raw_memory<std::string> m2(4);
    fixed_vector<std::string> a(empty_in(m1));
    fixed_vector<std::string> b(empty_in(m2));
    a.push_back("x");
    a.push_back("y");
    b.push_back("x");
    b.push_back("y");
    CHECK(a == b); // the capacities do not matter
    b.push_back("z");
    CHECK(a != b);
    CHECK(a < b);
    CHECK((a <=> b) == std::strong_ordering::less);
}
