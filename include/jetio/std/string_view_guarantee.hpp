#pragma once

// What a string view knows about the memory at and after its end. See
// docs/design/string-views.md.
//
//   terminated    nt: data()[size()] == CharT()
//   sentinel      ns: scanning from the end reaches a CharT() without leaving valid memory
//   determinable  nd: data()[size()] can be read, so "is it CharT()?" can be answered
//   none          std::basic_string_view
//
// Each guarantee is stronger than all that follow. A type states its guarantee with
// end_guarantee_of; a type with c_str() (or an ADL get_c_str) that converts to
// std::basic_string_view is terminated by default.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jetio
{
    template <typename T>
    concept character =
        std::same_as<std::remove_cv_t<T>, char> || std::same_as<std::remove_cv_t<T>, wchar_t> ||
        std::same_as<std::remove_cv_t<T>, char8_t> || std::same_as<std::remove_cv_t<T>, char16_t> ||
        std::same_as<std::remove_cv_t<T>, char32_t>;

    /// How many elements the memory behind a pointer may be read, counted from that pointer.
    /// Passed explicitly wherever a pointer string enters the library, so that a scan for the
    /// terminator is bounded.
    struct src_capacity_t
    {
        std::size_t value;
    };

    constexpr src_capacity_t src_capacity(std::size_t elements) noexcept
    {
        return {elements};
    }

    /// A pointer to constant characters, as returned by c_str().
    template <typename T>
    concept c_str_pointer = std::is_pointer_v<T> && character<std::remove_pointer_t<T>> &&
                            std::is_const_v<std::remove_pointer_t<T>>;

    namespace detail::get_c_str_cpo
    {
        void get_c_str() = delete; // poison pill: only ADL finds a customization

        template <typename T>
        concept has_member = requires(const T &t) {
            { t.c_str() } -> c_str_pointer;
        };

        template <typename T>
        concept has_adl = !has_member<T> && requires(const T &t) {
            { get_c_str(t) } -> c_str_pointer;
        };

        struct fn
        {
            template <typename T>
                requires has_member<T>
            constexpr auto operator()(const T &t) const noexcept(noexcept(t.c_str()))
            {
                return t.c_str();
            }

            template <typename T>
                requires has_adl<T>
            constexpr auto operator()(const T &t) const noexcept(noexcept(get_c_str(t)))
            {
                return get_c_str(t);
            }
        };
    } // namespace detail::get_c_str_cpo

    /// The pointer to the terminated characters of an object: its member c_str(), or an
    /// get_c_str(x) found by ADL. A customization point for types that are not ours.
    inline constexpr detail::get_c_str_cpo::fn get_c_str{};

    enum class end_guarantee : std::uint8_t
    {
        none,
        determinable,
        sentinel,
        terminated
    };

    namespace detail
    {
        template <typename T>
        concept has_c_str = requires(const T &t) {
            { jetio::get_c_str(t) } -> c_str_pointer;
        };

        template <typename T> struct char_type_of
        {
        };

        template <has_c_str T> struct char_type_of<T>
        {
            using type = std::remove_cv_t<
                std::remove_pointer_t<decltype(jetio::get_c_str(std::declval<const T &>()))>>;
        };

        template <typename T>
            requires(!has_c_str<T> && requires { typename T::value_type; })
        struct char_type_of<T>
        {
            using type = typename T::value_type;
        };

        template <typename T>
        concept has_char_type = requires { typename char_type_of<T>::type; };

        template <typename T> constexpr end_guarantee default_end_guarantee() noexcept
        {
            if constexpr (has_c_str<T>)
            {
                if constexpr (std::convertible_to<const T &, std::basic_string_view<
                                                                 typename char_type_of<T>::type>>)
                    return end_guarantee::terminated;
                else
                    return end_guarantee::none;
            }
            else
            {
                return end_guarantee::none;
            }
        }
    } // namespace detail

    namespace detail
    {
        /// What a function taking a string may be given: anything that converts to the string
        /// view, except a pointer or an array. Those do not enter as strings implicitly.
        template <typename U, typename StringView>
        concept string_view_argument =
            !std::is_pointer_v<std::decay_t<U>> && std::convertible_to<const U &, StringView>;
    } // namespace detail

    /// The character type of a string-like type: the pointee of its c_str(), else value_type.
    template <typename T>
        requires detail::has_char_type<std::remove_cvref_t<T>>
    using char_type_t = typename detail::char_type_of<std::remove_cvref_t<T>>::type;

    /// The guarantee of a type, for T without cv and reference. Specialize it to state the
    /// guarantee of a type that has no c_str(), or to withdraw it from one that has.
    template <typename T>
    inline constexpr end_guarantee end_guarantee_of = detail::default_end_guarantee<T>();

    template <typename T>
    inline constexpr end_guarantee end_guarantee_v = end_guarantee_of<std::remove_cvref_t<T>>;

    /// Converts to std::basic_string_view of its own character type. The base of the concepts
    /// below, which add the guarantee as a refinement.
    template <typename T>
    concept string_view_convertible =
        detail::has_char_type<std::remove_cvref_t<T>> &&
        std::convertible_to<const std::remove_cvref_t<T> &, std::basic_string_view<char_type_t<T>>>;

    // Each concept names the weaker one, so nt subsumes ns subsumes nd subsumes the base, and
    // overloads on them pick the most specific.
    template <typename T>
    concept nd_string_view_like =
        string_view_convertible<T> && (end_guarantee_v<T> >= end_guarantee::determinable);

    template <typename T>
    concept ns_string_view_like =
        nd_string_view_like<T> && (end_guarantee_v<T> >= end_guarantee::sentinel);

    template <typename T>
    concept nt_string_view_like =
        ns_string_view_like<T> && (end_guarantee_v<T> >= end_guarantee::terminated);
} // namespace jetio
