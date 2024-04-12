#ifndef __MISC_H__
#define __MISC_H__

#include <array>
#include <utility>

namespace plz
{

template <typename T>
struct is_array_specialization : std::false_type
{
};

template <typename T, size_t N>
struct is_array_specialization<std::array<T, N>> : std::true_type
{
};

template <typename T>
struct array_traits
{
};

template <typename T, size_t N>
struct array_traits<std::array<T, N>>
{
  using value_type                 = T;
  static constexpr size_t capacity = N;
};

namespace detail
{
template <typename T, std::size_t... Is>
constexpr std::array<T, sizeof...(Is)> create_array(T value, std::index_sequence<Is...>)
{
  // cast is to avoid to remove the warning: unused value
  return { { (static_cast<void>(Is), std::move(value))... } };
}

template <typename T, typename... Args, std::size_t... Is>
constexpr std::array<T, sizeof...(Is)>
make_array_in_place(std::index_sequence<Is...>, Args&&... args)
{
  // cast is to avoid to remove the warning: unused value
  return { { (static_cast<void>(Is), T(std::forward<Args>(args)...))... } };
}

} // namespace detail

template <std::size_t N, typename T>
constexpr std::array<T, N> create_array(T value)
{
  return detail::create_array(std::move(value), std::make_index_sequence<N>());
}

template <std::size_t N, typename T, typename... Args>
constexpr std::array<T, N> make_array_in_place(Args&&... args)
{
  return detail::make_array_in_place<T>(
    std::make_index_sequence<N>(), std::forward<Args>(args)...);
}

} // namespace plz
#endif // __MISC_H__