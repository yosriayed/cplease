#ifndef TYPE_TRAITS_H
#define TYPE_TRAITS_H

#include <tuple>
#include <type_traits>
#include <format>

namespace plz
{

// clang-format off
///
/// is_specialization_of is a type trait that checks if a given type is a
/// specialization of a given template.
///
/// Example:
/// static_assert(is_specialization_of_v<std::vector<int>, std::vector> == true);
/// static_assert(is_specialization_of_v<std::vector<int>, std::list> == false);
///
/// Limitation: it only works for templates with type parameters, e.g. it doesn't work for std::array<int, 5>
///
// clang-format on

template <typename Class, template <typename...> class Template>
struct is_specialization_of : std::false_type
{
};

template <template <typename...> class Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type
{
};

template <typename Class, template <typename...> class Template>
constexpr auto is_specialization_of_v = is_specialization_of<Class, Template>::value;

template <typename Class, template <typename...> typename Template>
concept specialization_of = is_specialization_of_v<Class, Template>;

// clang-format off
///
/// get_template_arg_type_of is a type trait that extracts the template
/// arguments of a given type.
///
/// Example:
/// static_assert(std::is_same_v<get_template_arg_type_of<std::vector<int>>::arguments,std::tuple<int, std::allocator<int>>>);
/// static_assert(get_template_arg_type_of<std::vector<int>>::nargs == 2);
/// static_assert(std::is_same_v<get_template_arg_type_of<std::vector<int>>::arg<0>::type, int>);
/// 
/// Limitation: it only works for templates with type parameters, e.g. it
/// doesn't work for std::array<int, 5>.
///
// clang-format on

template <typename T>
struct get_template_arg_type_of;

template <template <typename...> typename C, typename... Args>
struct get_template_arg_type_of<C<Args...>>
{
  using arguments           = std::tuple<Args...>;
  static const size_t nargs = sizeof...(Args);
  template <size_t i>
  struct arg
  {
    using type = typename std::tuple_element<i, arguments>::type;
  };
};

// clang-format off
///
/// function_traits is a type trait that extracts the return type and arguments types
/// of a function.
///
/// Example:
/// static_assert(std::is_same_v<function_traits<int(int, int)>::return_type, int>;
/// static_assert(std::is_same_v<function_traits<int(int, int)>::arguments, std::tuple<int, int>>);
/// static_assert(function_traits<int(int, int)>::nargs == 2);
/// static_assert(std::is_same_v<function_traits<int(int, int)>::arg<0>::type, int>);
/// static_assert(std::is_same_v<function_traits<int(int, std::string)>::arg<1>::type, std::string>);
///
// clang-format on

template <typename Function>
struct function_traits;

// Partial specialization for regular functions and lambdas
template <typename Ret, typename... Args>
struct function_traits<Ret(Args...)>
{
  using return_type = Ret;
  using arguments   = std::tuple<Args...>;

  static const size_t nargs = sizeof...(Args);
  template <size_t i>
  struct arg
  {
    typedef typename std::tuple_element<i, arguments>::type type;
  };
};

// Partial specialization for function pointers
template <typename Ret, typename... Args>
struct function_traits<Ret (*)(Args...)> : function_traits<Ret(Args...)>
{
};

// Partial specialization for member function pointers
template <typename Class, typename Ret, typename... Args>
struct function_traits<Ret (Class::*)(Args...)> : function_traits<Ret(Args...)>
{
};

// Partial specialization for const member function pointers
template <typename Class, typename Ret, typename... Args>
struct function_traits<Ret (Class::*)(Args...) const> : function_traits<Ret(Args...)>
{
};

// Partial specialization for lambda functions
template <typename Lambda>
struct function_traits : function_traits<decltype(&Lambda::operator())>
{
};

template <typename T>
concept is_formattable = requires(T& v, std::format_context ctx) {
  std::formatter<std::remove_cvref_t<T>>().format(v, ctx);
};

} // namespace plz

#endif
