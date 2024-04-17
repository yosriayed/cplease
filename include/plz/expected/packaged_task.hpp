#ifndef __EXPECTED_TASK_H__
#define __EXPECTED_TASK_H__

#ifdef WITH_STD_EXPECTED

#include <functional>
#include <iostream>
#include <memory>
#include <stop_token>
#include <tuple>
#include <type_traits>

#include "plz/help/type_traits.hpp"
#include "plz/packaged_task_base.hpp"

#include "future.hpp"

namespace plz::expected
{

template <typename Function, typename... Args>
  requires std::invocable<Function, Args...> &&
  specialization_of<std::invoke_result_t<Function, Args...>, std::expected>
class packaged_task : public packaged_task_base
{
  friend class plz::thread_pool;

  public:
  using function_arguments = typename std::tuple<std::decay_t<Args>...>;

  using function_return_type = typename std::invoke_result_t<Function, Args...>;

  using expected_value_type = typename function_return_type::value_type;
  using expected_error_type = typename function_return_type::error_type;

  using function_type = std::function<function_return_type(Args...)>;

  using future_type = future<expected_value_type, expected_error_type>;

  packaged_task(Function&& func, Args&&... t_args)
    : m_func{ std::forward<Function>(func) },
      m_arguments{ std::forward<Args>(t_args)... },
      m_promise{ make_promise<expected_value_type, expected_error_type>() }
  {
  }

  packaged_task(const packaged_task&)            = delete;
  packaged_task& operator=(const packaged_task&) = delete;
  packaged_task(packaged_task&& t_other)
  {
    std::swap(m_func, t_other.m_func);
    std::swap(m_arguments, t_other.m_arguments);
    std::swap(m_promise, t_other.m_promise);
  }

  void run(std::stop_token thread_stop_token) override
  {
    m_promise.set_result(std::apply(m_func, m_arguments));
  }

  future_type get_future()
  {
    return m_promise.get_future();
  }

  private:
  function_type m_func;
  function_arguments m_arguments;
  promise<expected_value_type, expected_error_type> m_promise;
};

template <typename Function, typename... Args>
  requires std::invocable<Function, Args..., std::stop_token> &&
  specialization_of<std::invoke_result_t<Function, Args..., std::stop_token>, std::expected>
class packaged_task_st : public packaged_task_base
{
  friend class plz::thread_pool;

  public:
  using function_arguments = typename std::tuple<std::decay_t<Args>...>;

  using function_return_type =
    typename std::invoke_result_t<Function, Args..., std::stop_token>;

  using expected_value_type = typename function_return_type::value_type;
  using expected_error_type = typename function_return_type::error_type;

  using function_type = std::function<function_return_type(Args..., std::stop_token)>;

  using future_type = future<expected_value_type, expected_error_type>;

  packaged_task_st(Function&& func, Args&&... t_args)
    : m_func{ std::forward<Function>(func) },
      m_arguments{ std::forward<Args>(t_args)... },
      m_promise{ make_promise<expected_value_type, expected_error_type>() }
  {
  }

  packaged_task_st(const packaged_task_st&)            = delete;
  packaged_task_st& operator=(const packaged_task_st&) = delete;
  packaged_task_st(packaged_task_st&& t_other)
  {
    std::swap(m_func, t_other.m_func);
    std::swap(m_arguments, t_other.m_arguments);
    std::swap(m_promise, t_other.m_promise);
    std::swap(m_stop_token, t_other.m_stop_token);
  }

  void run(std::stop_token thread_stop_token) override
  {
    m_promise.set_result(std::apply(
      m_func, std::tuple_cat(m_arguments, std::make_tuple(thread_stop_token))));
  }

  future<expected_value_type, expected_error_type> get_future() const
  {
    return m_promise.get_future();
  }

  private:
  function_type m_func;
  function_arguments m_arguments;
  promise<expected_value_type, expected_error_type> m_promise;
  std::stop_token m_stop_token;
};
} // namespace plz::expected

#endif // WITH_STD_EXPECTED

#endif // __EXPECTED_TASK_H__