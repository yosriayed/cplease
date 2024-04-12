#ifndef __EXPECTED_FUTURE_H__
#define __EXPECTED_FUTURE_H__

#ifdef HAVE_STD_EXPECTED

#include <cassert>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include "plz/help/type_traits.hpp"

namespace plz::async
{
class thread_pool;
}

namespace plz::async::expected
{

template <typename T, typename E>
class promise;

template <typename T, typename E>
class future;

template <typename T, typename E>
auto make_promise() noexcept -> promise<T, E>;

namespace detail
{

template <typename T, typename E>
class future_state
{
  template <typename X, typename Y>
  friend class plz::async::expected::promise;

  template <typename X, typename Y>
  friend class plz::async::expected::future;

  friend class plz::async::thread_pool;

  using result_type = T;
  using error_type  = E;

  mutable std::mutex m_mutex;
  std::condition_variable m_condition_variable;
  std::expected<result_type, error_type> m_result;
  bool m_is_ready{ false };

  std::vector<std::function<void(const result_type&)>> m_success_handlers;
  std::vector<std::function<void(const error_type&)>> m_error_handlers;

  thread_pool* m_pool{};

  void on_ready() noexcept
  {
    assert(m_is_ready);
    if(!m_result)
    {
      for(auto& cb : m_error_handlers)
      {
        cb(m_result.error());
      }
    }
    else
    {
      for(auto& cb : m_success_handlers)
      {
        cb(*m_result);
      }
    }

    m_condition_variable.notify_all();
  }
};

template <typename E>
class future_state<void, E>
{
  template <typename X, typename Y>
  friend class plz::async::expected::promise;

  template <typename X, typename Y>
  friend class plz::async::expected::future;

  friend class plz::async::thread_pool;

  using result_type = void;
  using error_type  = E;

  mutable std::mutex m_mutex;
  std::condition_variable m_condition_variable;
  std::expected<result_type, error_type> m_result;
  bool m_is_ready{ false };

  std::vector<std::function<void()>> m_success_handlers;
  std::vector<std::function<void(const error_type&)>> m_error_handlers;

  thread_pool* m_pool{};

  void on_ready() noexcept
  {
    assert(m_is_ready);
    if(!m_result)
    {
      for(auto& cb : m_error_handlers)
      {
        cb(m_result.error());
      }
    }
    else
    {
      for(auto& cb : m_success_handlers)
      {
        cb();
      }
    }

    m_condition_variable.notify_all();
  }
};

} // namespace detail
template <typename T, typename E>
class future
{
  template <typename X, typename Y>
  friend class expected::promise;

  public:
  using result_type = T;
  using error_type  = E;

  //! Wait for the future to produce a result before allowing the calling thread to proceed.
  auto wait() noexcept -> std::expected<result_type, error_type>
  {
    std::unique_lock lock(m_shared_state->m_mutex);
    m_shared_state->m_condition_variable.wait(lock,
      [this]
      {
        return m_shared_state->m_is_ready;
      });

    if constexpr(std::is_copy_constructible_v<std::expected<result_type, error_type>>)
    {
      return m_shared_state->m_result;
    }
    else
    {
      m_shared_state->m_is_ready = false;
      return std::move(m_shared_state->m_result);
    }
  }

  auto take() -> std::expected<result_type, error_type>
  {
    std::unique_lock lock(m_shared_state->m_mutex);

    m_shared_state->m_condition_variable.wait(lock,
      [this]()
      {
        return m_shared_state->m_is_ready;
      });

    m_shared_state->m_is_ready = false;

    return std::move(m_shared_state->m_result);
  }
  //! Register an error handler to be invoked when an exception of matching function parameter.
  template <typename Func>
  auto& on_error(Func&& t_func) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_error_handlers.push_back(t_func);

    return *this;
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a callback that returns void
  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, result_type, Args...>, void>
  auto& then(Func&& t_func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func, ... args = std::forward<Args>(args)](const result_type& value) mutable
      {
        t_func(value, args...);
      });

    return *this;
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a member function that returns void
  template <typename Context, typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Context&, result_type, Args...>, void> &&
    (!std::same_as<Context, thread_pool>)
  auto& then(Context* t_context, Func t_func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_context, t_func, ... args = std::forward<Args>(args)](const result_type& value)
      {
        (t_context->*t_func)(value, args...);
      });

    return *this;
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a member function that returns AnyType that is different from void
  template <typename Func, typename... Args>
    requires(!plz::specialization_of<std::invoke_result_t<Func, result_type, Args...>, future>) &&
    (!std::same_as<std::invoke_result_t<Func, result_type, Args...>, void>) &&
    (!plz::specialization_of<std::invoke_result_t<Func, result_type, Args...>, std::expected>)
  auto then(Func&& t_func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise = make_promise<func_return_type, error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          t_func(value, args...);
          promise.set_ready();
        }
        else
        {
          promise.set_result(t_func(value, args...));
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a member function that returns AnyType that is different from void
  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, result_type, Args...>, std::expected>
  auto then(Func&& t_func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    using future_result_type =
      typename get_template_arg_type_of<func_return_type>::template arg<0>::type;

    using future_error_type =
      typename get_template_arg_type_of<func_return_type>::template arg<1>::type;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise = make_promise<future_result_type, future_error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          auto result = t_func(value, args...);
          if(result)
          {
            promise.set_ready();
          }
          else
          {
            promise.set_error(result.error());
          }
        }
        else
        {
          auto result = t_func(value, args...);
          if(result)
          {
            promise.set_result(*result);
          }
          else
          {
            promise.set_error(result.error());
          }
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a callback that returns future<AnyType>
  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, result_type, Args...>, future>
  auto then(Func&& t_func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    using future_result_type =
      typename get_template_arg_type_of<func_return_type>::template arg<0>::type;

    using future_error_type =
      typename get_template_arg_type_of<func_return_type>::template arg<1>::type;

    auto promise = make_promise<future_result_type, future_error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        if constexpr(std::is_void_v<future_result_type>)
        {
          t_func(value, args...)
            .then(
              [promise]()
              {
                promise.set_ready();
              })
            .on_error(
              [promise](const auto& error)
              {
                promise.set_error(error);
              });
        }
        else
        {
          t_func(value, args...)
            .then(
              [promise](const auto& result) mutable
              {
                promise.set_result(result);
              })
            .on_error(
              [promise](const auto& error) mutable
              {
                promise.set_error(error);
              });
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  template <typename Context, typename Func, typename... Args>
    requires(!std::same_as<std::invoke_result_t<Func, Context&, result_type, Args...>, void>) &&
    (!std::same_as<Context, thread_pool>)
  auto then(Context* t_context, Func&& t_func, Args&&... args) noexcept
  {
    return then(
      [t_context, t_func = std::move(t_func), ... args = std::forward<Args>(args)](
        const auto& value)
      {
        return (t_context->*t_func)(value, args...);
      });
  }

  template <typename Func, typename... Args>
  auto then(thread_pool* pool, Func&& t_func, Args&&... args);

  template <typename Func, typename... Args>
  auto async_then(Func&& t_func, Args&&... args)
  {
    assert(m_shared_state->m_pool);
    return then(
      m_shared_state->m_pool, std::forward<Func>(t_func), std::forward<Args>(args)...);
  }

  private:
  future(std::shared_ptr<detail::future_state<result_type, error_type>> t_shared_state)
    : m_shared_state{ std::move(t_shared_state) }
  {
  }

  std::shared_ptr<detail::future_state<result_type, error_type>> m_shared_state;
};

template <typename T, typename E>
class promise
{
  template <typename X, typename Y>
  friend class future;

  friend class plz::async::thread_pool;

  public:
  promise() : m_shared_state{ std::make_shared<detail::future_state<T, E>>() }
  {
  }

  using result_type = T;
  using error_type  = E;

  auto get_future() const noexcept -> future<T, E>
  {
    return future<T, E>(m_shared_state);
  }

  template <typename U>
    requires std::convertible_to<U, result_type>
  void set_result(U&& t_result)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    if constexpr(std::is_copy_assignable_v<result_type>)
    {
      m_shared_state->m_result = std::forward<U>(t_result);
    }
    else
    {
      m_shared_state->m_result = std::forward<U>(std::move(t_result));
    }

    m_shared_state->m_is_ready = true;

    m_shared_state->on_ready();
  }

  template <typename U = result_type>
    requires std::same_as<U, void>
  void set_ready()
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    m_shared_state->m_is_ready = true;
    m_shared_state->m_result   = {};

    m_shared_state->on_ready();
  }

  template <typename U>
    requires std::convertible_to<U, error_type>
  void set_result(std::unexpected<U>&& t_result)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    m_shared_state->m_result   = t_result;
    m_shared_state->m_is_ready = true;

    m_shared_state->on_ready();
  }

  template <typename U>
    requires std::same_as<U, std::expected<result_type, error_type>>
  void set_result(const U& t_result)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    m_shared_state->m_result   = t_result;
    m_shared_state->m_is_ready = true;

    m_shared_state->on_ready();
  }

  template <typename U>
    requires std::convertible_to<U, error_type>
  void set_error(const U& t_error)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    m_shared_state->m_result   = std::unexpected(t_error);
    m_shared_state->m_is_ready = true;

    m_shared_state->on_ready();
  }

  private:
  std::shared_ptr<detail::future_state<result_type, error_type>> m_shared_state;
};

template <typename E>
class future<void, E>
{
  template <typename X, typename Y>
  friend class promise;

  public:
  using result_type = void;
  using error_type  = E;

  //! Wait for the future to produce a result before allowing the calling thread to proceed.
  auto wait() const noexcept -> std::expected<result_type, error_type>
  {
    std::unique_lock lock(m_shared_state->m_mutex);
    m_shared_state->m_condition_variable.wait(lock,
      [this]
      {
        return m_shared_state->m_is_ready;
      });
    return m_shared_state->m_result;
  }

  //! Register an error handler to be invoked when an exception of matching function parameter.
  template <typename Func>
  auto& on_error(Func&& t_func) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(t_func);

    return *this;
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a callback that returns void
  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Args...>, void>
  auto& then(Func&& t_func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func, ... args = std::forward<Args>(args)]() mutable
      {
        t_func(args...);
      });

    return *this;
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a member function that returns void
  template <typename Context, typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Context&, Args...>, void>
  auto& then(Context* t_context, Func t_func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_context, t_func, ... args = std::forward<Args>(args)]()
      {
        (t_context->*t_func)(args...);
      });

    return *this;
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a member function that returns AnyType that is different from void
  template <typename Func, typename... Args>
    requires(!plz::specialization_of<std::invoke_result_t<Func, Args...>, future>) &&
    (!std::same_as<std::invoke_result_t<Func, Args...>, void>) &&
    (!plz::specialization_of<std::invoke_result_t<Func, Args...>, std::expected>)
  auto then(Func&& t_func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, Args...>;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise = make_promise<func_return_type, error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          t_func(args...);
          promise.set_ready();
        }
        else
        {
          promise.set_result(t_func(args...));
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a member function that returns AnyType that is different from void
  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, Args...>, std::expected>
  auto then(Func&& t_func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, Args...>;

    using future_result_type =
      typename get_template_arg_type_of<func_return_type>::template arg<0>::type;

    using future_error_type =
      typename get_template_arg_type_of<func_return_type>::template arg<1>::type;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise = make_promise<future_result_type, future_error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          auto result = t_func(args...);
          if(result)
          {
            promise.set_ready();
          }
          else
          {
            promise.set_error(result.error());
          }
        }
        else
        {
          auto result = t_func(args...);
          if(result)
          {
            promise.set_result(*result);
          }
          else
          {
            promise.set_error(result.error());
          }
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a callback that returns future<AnyType>
  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, result_type, Args...>, future>
  auto then(Func&& t_func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    using future_result_type =
      typename get_template_arg_type_of<func_return_type>::template arg<0>::type;

    using future_error_type =
      typename get_template_arg_type_of<func_return_type>::template arg<1>::type;

    auto promise = make_promise<future_result_type, future_error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        if constexpr(std::is_void_v<future_result_type>)
        {
          t_func(args...)
            .then(
              [promise]()
              {
                promise.set_ready();
              })
            .on_error(
              [promise](const auto& error)
              {
                promise.set_error(error);
              });
        }
        else
        {
          t_func(args...)
            .then(
              [promise](const auto& result) mutable
              {
                promise.set_result(result);
              })
            .on_error(
              [promise](const auto& error) mutable
              {
                promise.set_error(error);
              });
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  template <typename Context, typename Func, typename... Args>
    requires(!std::same_as<std::invoke_result_t<Func, Context&, Args...>, void>)
  auto then(Context* t_context, Func&& t_func, Args&&... args) noexcept
  {
    return then(
      [t_context, t_func = std::move(t_func), ... args = std::forward<Args>(args)](
        const auto& value)
      {
        return (t_context->*t_func)(value, args...);
      });
  }

  template <typename Func, typename... Args>
  auto async_then(thread_pool* pool, Func&& t_func, Args&&... args);

  template <typename Func, typename... Args>
  auto async_then(Func&& t_func, Args&&... args)
  {
    assert(m_shared_state->m_pool);
    return async_then(
      m_shared_state->m_pool, std::forward<Func>(t_func), std::forward<Args>(args)...);
  }

  private:
  future(std::shared_ptr<detail::future_state<result_type, error_type>> t_shared_state)
    : m_shared_state{ std::move(t_shared_state) }
  {
  }

  std::shared_ptr<detail::future_state<result_type, error_type>> m_shared_state;
};

template <typename T, typename E>
auto make_promise() noexcept -> promise<T, E>
{
  return promise<T, E>{};
}

} // namespace plz::async::expected

#endif // HAVE_STD_EXPECTED
#endif // __EXPECTED_FUTURE_H__