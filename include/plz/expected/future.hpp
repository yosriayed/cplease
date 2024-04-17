#ifndef __EXPECTED_FUTURE_H__
#define __EXPECTED_FUTURE_H__

#include <__expected/unexpected.h>
#ifdef WITH_STD_EXPECTED

#include <cassert>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include "plz/help/type_traits.hpp"

//////////////////////////
// Forward declarations //
/////////////////////// //

namespace plz
{
class thread_pool;
}

namespace plz::expected
{

template <typename T, typename E>
class promise;

template <typename T, typename E>
class future;

template <typename T, typename E>
auto make_promise() noexcept -> promise<T, E>;

////////////////////////////
// Implementation details //
////////////////////////////

namespace detail
{

///////////////////////////////////////////////////////////////////////////////
/// Internal class that holds the shared state of the future/promise objects //
///////////////////////////////////////////////////////////////////////////////

template <typename T, typename E>
class state
{
  template <typename X, typename Y>
  friend class plz::expected::promise;

  template <typename X, typename Y>
  friend class plz::expected::future;

  friend class plz::thread_pool;

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

//////////////////////////////////////////////
// partial specialization of state<void, E> //
//////////////////////////////////////////////

template <typename E>
class state<void, E>
{
  template <typename X, typename Y>
  friend class plz::expected::promise;

  template <typename X, typename Y>
  friend class plz::expected::future;

  friend class plz::thread_pool;

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

/////////////////////////////
// future primary template //
/////////////////////////////

template <typename T, typename E>
class future
{
  template <typename X, typename Y>
  friend class expected::promise;

  public:
  using result_type = T;
  using error_type  = E;

  auto get() noexcept -> std::expected<result_type, error_type>
    requires(std::is_copy_constructible_v<std::expected<result_type, error_type>>)
  {
    std::unique_lock lock(m_shared_state->m_mutex);
    m_shared_state->m_condition_variable.wait(lock,
      [this]
      {
        return m_shared_state->m_is_ready;
      });

    return m_shared_state->m_result;
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

  template <typename Func>
  auto& on_error(Func&& func) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_error_handlers.push_back(func);

    return *this;
  }

  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, result_type, Args...>, void>
  auto& then(Func&& func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [func, ... args = std::forward<Args>(args)](const result_type& value) mutable
      {
        func(value, args...);
      });

    return *this;
  }

  template <typename Context, typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Context&, result_type, Args...>, void> &&
    (!std::same_as<Context, thread_pool>)
  auto& then(Context* context, Func func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [context, func, ... args = std::forward<Args>(args)](const result_type& value)
      {
        (context->*func)(value, args...);
      });

    return *this;
  }

  template <typename Func, typename... Args>
    requires(!plz::specialization_of<std::invoke_result_t<Func, result_type, Args...>, future>) &&
    (!std::same_as<std::invoke_result_t<Func, result_type, Args...>, void>) &&
    (!plz::specialization_of<std::invoke_result_t<Func, result_type, Args...>, std::expected>)
  auto then(Func&& func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise = make_promise<func_return_type, error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [func = std::move(func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          func(value, args...);
          promise.set_ready();
        }
        else
        {
          promise.set_result(func(value, args...));
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, result_type, Args...>, std::expected>
  auto then(Func&& func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    using future_result_type =
      typename get_template_arg_type_of<func_return_type>::template arg<0>::type;

    using future_error_type =
      typename get_template_arg_type_of<func_return_type>::template arg<1>::type;

    static_assert(std::is_convertible_v<error_type, future_error_type>, "error type mismatch");

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise = make_promise<future_result_type, future_error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [func = std::move(func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          auto result = func(value, args...);
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
          auto result = func(value, args...);
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

  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, result_type, Args...>, future>
  auto then(Func&& func, Args&&... args) noexcept
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
      [func = std::move(func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        if constexpr(std::is_void_v<future_result_type>)
        {
          func(value, args...)
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
          func(value, args...)
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

  template <typename Context, typename Func, typename... Args>
    requires(!std::same_as<std::invoke_result_t<Func, Context&, result_type, Args...>, void>) &&
    (!std::same_as<Context, thread_pool>)
  auto then(Context* context, Func&& func, Args&&... args) noexcept
  {
    return then(
      [context, func = std::move(func), ... args = std::forward<Args>(args)](const auto& value)
      {
        return (context->*func)(value, args...);
      });
  }

  template <typename Func, typename... Args>
  auto then(thread_pool* pool, Func&& func, Args&&... args);

  template <typename Func, typename... Args>
  auto async_then(Func&& func, Args&&... args)
  {
    assert(m_shared_state->m_pool);
    return then(
      m_shared_state->m_pool, std::forward<Func>(func), std::forward<Args>(args)...);
  }

  private:
  future(std::shared_ptr<detail::state<result_type, error_type>> t_shared_state)
    : m_shared_state{ std::move(t_shared_state) }
  {
  }

  std::shared_ptr<detail::state<result_type, error_type>> m_shared_state;
};

////////////////////////////////////////////
// future<void, E> partial specialisation //
////////////////////////////////////////////

template <typename E>
class future<void, E>
{
  template <typename X, typename Y>
  friend class promise;

  public:
  using result_type = void;
  using error_type  = E;

  auto get() const noexcept -> std::expected<result_type, error_type>
  {
    std::unique_lock lock(m_shared_state->m_mutex);
    m_shared_state->m_condition_variable.wait(lock,
      [this]
      {
        return m_shared_state->m_is_ready;
      });
    return m_shared_state->m_result;
  }

  template <typename Func>
  auto& on_error(Func&& func) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(func);

    return *this;
  }

  //! Attaches a continuation to this future, allowing to chain multiple asynchronous computations
  // Overload for a callback that returns void
  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Args...>, void>
  auto& then(Func&& func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [func, ... args = std::forward<Args>(args)]() mutable
      {
        func(args...);
      });

    return *this;
  }

  template <typename Context, typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Context&, Args...>, void>
  auto& then(Context* context, Func func, Args&&... args) noexcept
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [context, func, ... args = std::forward<Args>(args)]()
      {
        (context->*func)(args...);
      });

    return *this;
  }
  
  template <typename Func, typename... Args>
    requires(!plz::specialization_of<std::invoke_result_t<Func, Args...>, future>) &&
    (!std::same_as<std::invoke_result_t<Func, Args...>, void>) &&
    (!plz::specialization_of<std::invoke_result_t<Func, Args...>, std::expected>)
  auto then(Func&& func, Args&&... args) noexcept
  {
    using func_return_type = typename std::invoke_result_t<Func, Args...>;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise = make_promise<func_return_type, error_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [func = std::move(func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          func(args...);
          promise.set_ready();
        }
        else
        {
          promise.set_result(func(args...));
        }
      });

    m_shared_state->m_error_handlers.push_back(
      [promise](const error_type& error) mutable
      {
        promise.set_error(error);
      });

    return promise.get_future();
  }

  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, Args...>, std::expected>
  auto then(Func&& func, Args&&... args) noexcept
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
      [func = std::move(func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        if constexpr(std::is_same_v<func_return_type, void>)
        {
          auto result = func(args...);
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
          auto result = func(args...);
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

  template <typename Func, typename... Args>
    requires specialization_of<std::invoke_result_t<Func, result_type, Args...>, future>
  auto then(Func&& func, Args&&... args) noexcept
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
      [func = std::move(func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        if constexpr(std::is_void_v<future_result_type>)
        {
          func(args...)
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
          func(args...)
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

  template <typename Context, typename Func, typename... Args>
    requires(!std::same_as<std::invoke_result_t<Func, Context&, Args...>, void>)
  auto then(Context* context, Func&& func, Args&&... args) noexcept
  {
    return then(
      [context, func = std::move(func), ... args = std::forward<Args>(args)](const auto& value)
      {
        return (context->*func)(value, args...);
      });
  }

  template <typename Func, typename... Args>
  auto async_then(thread_pool* pool, Func&& func, Args&&... args);

  template <typename Func, typename... Args>
  auto async_then(Func&& func, Args&&... args)
  {
    assert(m_shared_state->m_pool);
    return async_then(
      m_shared_state->m_pool, std::forward<Func>(func), std::forward<Args>(args)...);
  }

  private:
  future(std::shared_ptr<detail::state<result_type, error_type>> t_shared_state)
    : m_shared_state{ std::move(t_shared_state) }
  {
  }

  std::shared_ptr<detail::state<result_type, error_type>> m_shared_state;
};

/////////////
// promise //
/////////////

template <typename T, typename E>
class promise
{
  template <typename X, typename Y>
  friend class future;

  friend class plz::thread_pool;

  public:
  promise() : m_shared_state{ std::make_shared<detail::state<T, E>>() }
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
  void set_result(U&& result)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    if constexpr(std::is_copy_assignable_v<result_type>)
    {
      m_shared_state->m_result = std::forward<U>(result);
    }
    else
    {
      m_shared_state->m_result = std::forward<U>(std::move(result));
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
  void set_result(std::unexpected<U>&& result)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    m_shared_state->m_result   = result;
    m_shared_state->m_is_ready = true;

    m_shared_state->on_ready();
  }

  template <typename U>
    requires std::same_as<U, std::expected<result_type, error_type>>
  void set_result(const U& result)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    m_shared_state->m_result   = result;
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
  std::shared_ptr<detail::state<result_type, error_type>> m_shared_state;
};

template <typename T, typename E>
auto make_promise() noexcept -> promise<T, E>
{
  return promise<T, E>{};
}

} // namespace plz::expected

#endif // WITH_STD_EXPECTED
#endif // __EXPECTED_FUTURE_H__