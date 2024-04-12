#ifndef __FUTURE_H__
#define __FUTURE_H__

// stl headers

#include <cassert>
#include <concepts>
#include <condition_variable>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>

#ifdef WITH_STD_EXPECTED
#include "plz/expected/future.hpp"
#endif

#include "plz/help/type_traits.hpp"

namespace plz::async
{

// The pair of future and promise classes are similiar to std::promise
// and std::future. They provide a mechanism to to access the result of asynchronous operations.

// The creator of the promise class can set a value
// using the set_result() or set an exception using set_exception(). The
// associated future object can retrieve the value using the wait() method which
// will block the calling thread until the promise is fulfilled.

// If an exception is thrown during the computation, the wait() method will rethrow the exception.

// The future class also provides a then() method that allows the user to attach
// an arbitray continuation to the result of the future, which will be executed
// when the result is ready. The then() method returns a new future that
// represents the result of the continuation.

// The future class also provides an on_exception() method that
// allows the user to attach an exception handler to the future, which will be
// executed when an exception is thrown during the computation.

// The exception handler can be a function that takes a std::exception_ptr as an
// argument which will handle any type of exception, or a function that takes a
// specific exception type as an argument which will be invoked if that specific
// expection was set on the promise. The on_exception() method returns a
// reference to the future object, so that multiple exception handlers can be
// attached to the same future.

//////////////////////////
// Forward declarations //
/////////////////////// //

class thread_pool;

template <typename T, std::ranges::range KeysRange>
class Promises;

template <typename T, std::ranges::range KeysRange>
class futures;

template <typename T>
class future;

template <typename T>
class promise;

template <typename T>
promise<T> make_promise() noexcept;

#ifdef WITH_STD_EXPECTED
template <typename T, typename E>
expected::promise<T, E> make_promise() noexcept;
#endif

////////////////////////////
// Implementation details //
////////////////////////////

namespace detail
{

///////////////////////////////////////////////////////////////////////////////
/// Internal class that holds the shared state of the future/promise objects //
///////////////////////////////////////////////////////////////////////////////

template <typename T>
class state
{
  template <typename X>
  friend class plz::async::future;

  template <typename X>
  friend class plz::async::promise;

  friend class plz::async::thread_pool;

  using result_type = T;

  mutable std::mutex m_mutex;
  std::condition_variable m_condition_variable;
  result_type m_result;
  bool m_is_ready{ false };
  std::exception_ptr m_exception{ nullptr };

  std::vector<std::function<void(const result_type&)>> m_success_handlers;
  std::vector<std::function<bool(const std::exception_ptr&)>> m_exceptions_handlers;

  thread_pool* m_pool{};

  void on_ready()
  {
    assert(m_is_ready);
    if(m_exception)
    {
      for(auto& cb : m_exceptions_handlers)
      {
        // break if exception was handled
        if(cb(m_exception))
        {
          break;
        }
      }
    }
    else
    {
      for(auto& cb : m_success_handlers)
      {
        cb(m_result);
      }
    }

    m_condition_variable.notify_all();
  }
};

////////////////////////////////////////////
// explicit specialization of state<void> //
////////////////////////////////////////////

template <>
class state<void>
{
  template <typename X>
  friend class plz::async::future;

  template <typename X>
  friend class plz::async::promise;

  friend class plz::async::thread_pool;

  mutable std::mutex m_mutex;
  std::condition_variable m_condition_variable;
  bool m_is_ready{ false };
  std::exception_ptr m_exception{ nullptr };

  std::vector<std::function<void()>> m_success_handlers;
  std::vector<std::function<bool(const std::exception_ptr&)>> m_exceptions_handlers;

  thread_pool* m_pool{};

  void on_ready()
  {
    assert(m_is_ready);
    if(m_exception)
    {
      for(auto& cb : m_exceptions_handlers)
      {
        // break if exception was handled
        if(cb(m_exception))
        {
          break;
        }
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
////////////////////////// //

template <typename T>
class future
{
  public:
  using result_type = T;

  result_type wait()
  {
    std::unique_lock lock(m_shared_state->m_mutex);

    m_shared_state->m_condition_variable.wait(lock,
      [this]()
      {
        return m_shared_state->m_is_ready;
      });

    if(m_shared_state->m_exception)
    {
      std::rethrow_exception(m_shared_state->m_exception);
    }

    if constexpr(!std::is_void_v<result_type>)
    {
      if constexpr(std::is_copy_constructible_v<result_type>)
      {
        return m_shared_state->m_result;
      }
      else
      {
        m_shared_state->m_is_ready = false;
        return std::move(m_shared_state->m_result);
      }
    }
  }

  result_type take()
    requires(!(std::is_same_v<result_type, void>))
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

  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, result_type, Args...>, void>
  auto& then(Func&& t_func, Args&&... args)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func, ... args = std::forward<Args>(args)](const result_type& value) mutable
      {
        t_func(value, args...);
      });

    return *this;
  }

  template <typename Context, typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Context&, result_type, Args...>, void> &&
    (!std::same_as<Context, thread_pool>)
  auto& then(Context* t_context, Func t_func, Args&&... args)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_context, t_func, ... args = std::forward<Args>(args)](const result_type& value)
      {
        (t_context->*t_func)(value, args...);
      });

    return *this;
  }

  template <typename Func, typename... Args>
    requires plz::is_specialization_of_v<std::invoke_result_t<Func, result_type, Args...>, future>
  auto then(Func&& t_func, Args&&... args)
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    using future_result_type =
      typename get_template_arg_type_of<func_return_type>::template arg<0>::type;

    auto promise                   = make_promise<future_result_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        try
        {
          if constexpr(std::is_void_v<future_result_type>)
          {
            t_func(value, args...)
              .then(
                [promise]()
                {
                  promise.set_ready();
                })
              .on_exception(
                [promise](const std::exception_ptr& exception)
                {
                  promise.set_exception(exception);
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
              .on_exception(
                [promise](const std::exception_ptr& exception) mutable
                {
                  promise.set_exception(exception);
                });
          }
        }
        catch(...)
        {
          promise.set_exception(std::current_exception());
        }
      });

    m_shared_state->m_exceptions_handlers.push_back(
      [promise](const std::exception_ptr& exception) mutable
      {
        promise.set_exception(exception);
        return true;
      });

    return promise.get_future();
  }

  template <typename Func, typename... Args>
    requires(
      (!plz::is_specialization_of_v<std::invoke_result_t<Func, result_type, Args...>, future>) &&
      (!std::is_same_v<std::invoke_result_t<Func, result_type, Args...>, void>))
  auto then(Func&& t_func, Args&&... args)
  {
    using func_return_type = typename std::invoke_result_t<Func, result_type, Args...>;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise                   = make_promise<func_return_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)](
        const result_type& value) mutable
      {
        try
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
        }
        catch(...)
        {
          promise.set_exception(std::current_exception());
        }
      });

    m_shared_state->m_exceptions_handlers.push_back(
      [promise](const std::exception_ptr& exception) mutable
      {
        promise.set_exception(std::move(exception));
        return true;
      });

    return promise.get_future();
  }

  template <typename Context, typename Func, typename... Args>
    requires(!std::same_as<std::invoke_result_t<Func, Context&, result_type, Args...>, void>) &&
    (!std::same_as<Context, thread_pool>)
  auto then(Context* t_context, Func&& t_func, Args&&... args)
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

  template <typename Func>
  auto& on_exception(Func&& t_exception_handler)
  {
    using ExceptionType = typename function_traits<Func>::template arg<0>::type;

    if constexpr(std::is_same_v<std::exception_ptr, typename std::decay<ExceptionType>::type>)
    {
      auto func = [t_exception_handler = std::move(t_exception_handler)](
                    const std::exception_ptr& ptr) mutable
      {
        t_exception_handler(ptr);
        return true;
      };

      on_exception_impl(std::move(func));
    }
    else
    {
      auto func = [exception_handler = std::move(t_exception_handler)](
                    const std::exception_ptr& ptr) mutable
      {
        try
        {
          std::rethrow_exception(ptr);
        }
        catch(const ExceptionType& exception)
        {
          exception_handler(exception);
          return true;
        }
        catch(...)
        {
          return false;
        }
      };

      on_exception_impl(std::move(func));
    }

    return *this;
  }

  private:
  void on_exception_impl(std::function<bool(const std::exception_ptr&)> failure_cb)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_exceptions_handlers.push_back(std::move(failure_cb));
  }

  friend class promise<result_type>;

  future(std::shared_ptr<detail::state<result_type>> state)
    : m_shared_state(std::move(state))
  {
  }

  std::shared_ptr<detail::state<result_type>> m_shared_state;
};

//////////////////////////////////////////
// future<void> explicit specialisation //
//////////////////////////////////////////

template <>
class future<void>
{
  public:
  using result_type = void;

  void wait() const
  {
    std::unique_lock lock(m_shared_state->m_mutex);

    m_shared_state->m_condition_variable.wait(lock,
      [this]()
      {
        return m_shared_state->m_is_ready;
      });

    if(m_shared_state->m_exception)
    {
      std::rethrow_exception(m_shared_state->m_exception);
    }
  }

  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Args...>, void>
  auto& then(Func&& t_func, Args&&... args)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func, ... args = std::forward<Args>(args)]() mutable
      {
        t_func(args...);
      });

    return *this;
  }

  template <typename Context, typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Context&, Args...>, void>
  auto& then(Context* t_context, Func t_func, Args&&... args)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_context, t_func, ... args = std::forward<Args>(args)]()
      {
        (t_context->*t_func)(args...);
      });

    return *this;
  }

  template <typename Func, typename... Args>
    requires plz::specialization_of<std::invoke_result_t<Func, Args...>, future>
  auto then(Func&& t_func, Args&&... args)
  {
    using func_return_type = typename std::invoke_result_t<Func, Args...>;

    using future_result_type =
      typename get_template_arg_type_of<func_return_type>::template arg<0>::type;

    auto promise                   = make_promise<func_return_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        try
        {
          if constexpr(std::is_void_v<future_result_type>)
          {
            t_func(args...)
              .then(
                [promise]()
                {
                  promise.set_ready();
                })
              .on_exception(
                [promise](const std::exception_ptr& exception)
                {
                  promise.set_exception(exception);
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
              .on_exception(
                [promise](const std::exception_ptr& exception) mutable
                {
                  promise.set_exception(exception);
                });
          }
        }
        catch(...)
        {
          promise.set_exception(std::current_exception());
        }
      });

    m_shared_state->m_exceptions_handlers.push_back(
      [promise](const std::exception_ptr& exception) mutable
      {
        promise.set_exception(exception);
        return true;
      });

    return promise.get_future();
  }

  template <typename Func, typename... Args>
    requires(!plz::specialization_of<std::invoke_result_t<Func, Args...>, future>) &&
    (!std::same_as<std::invoke_result_t<Func, Args...>, void>)
  auto then(Func&& t_func, Args&&... args)
  {
    using func_return_type = typename std::invoke_result_t<Func, Args...>;

    std::lock_guard lock(m_shared_state->m_mutex);

    auto promise                   = make_promise<func_return_type>();
    promise.m_shared_state->m_pool = m_shared_state->m_pool;

    m_shared_state->m_success_handlers.push_back(
      [t_func = std::move(t_func), promise, ... args = std::forward<Args>(args)]() mutable
      {
        try
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
        }
        catch(...)
        {
          promise.set_exception(std::current_exception());
        }
      });

    m_shared_state->m_exceptions_handlers.push_back(
      [promise](const std::exception_ptr& exception) mutable
      {
        promise.set_exception(std::move(exception));
        return true;
      });

    return promise.get_future();
  }

  template <typename Context, typename Func, typename... Args>
    requires(!std::same_as<std::invoke_result_t<Func, Context&, Args...>, void>)
  auto then(Context* t_context, Func&& t_func, Args&&... args)
  {
    return then(
      [t_context, t_func = std::move(t_func), ... args = std::forward<Args>(args)]()
      {
        return (t_context->*t_func)(args...);
      });
  }

  //! Register an exception handler to be invoked when an exception of matching function parameter.
  template <typename Func>
  auto& on_exception(Func&& t_exception_handler)
  {
    using exception_type = typename function_traits<Func>::template arg<0>::type;

    if constexpr(std::is_same_v<std::exception_ptr, typename std::decay<exception_type>::type>)
    {
      auto func = [t_exception_handler = std::move(t_exception_handler)](
                    const std::exception_ptr& ptr) mutable
      {
        t_exception_handler(ptr);
        return true;
      };

      on_exception_impl(std::move(func));
    }
    else
    {
      auto func = [exception_handler = std::move(t_exception_handler)](
                    const std::exception_ptr& ptr) mutable
      {
        try
        {
          std::rethrow_exception(ptr);
        }
        catch(const exception_type& exception)
        {
          exception_handler(exception);
          return true;
        }
        catch(...)
        {
          return false;
        }
      };

      on_exception_impl(std::move(func));
    }

    return *this;
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
  void on_exception_impl(std::function<bool(const std::exception_ptr&)> failure_cb)
  {
    std::lock_guard lock(m_shared_state->m_mutex);

    m_shared_state->m_exceptions_handlers.push_back(std::move(failure_cb));
  }

  friend class promise<void>;

  future(std::shared_ptr<detail::state<void>> state)
    : m_shared_state(std::move(state))
  {
  }

  std::shared_ptr<detail::state<void>> m_shared_state;
};

/////////////
// promise //
/////////////

template <typename T>
class promise
{
  public:
  promise() : m_shared_state(std::make_shared<detail::state<T>>())
  {
  }

  using result_type = T;

  private:
  template <typename X>
  friend class future;

  template <typename X, std::ranges::range KeysRange>
  friend class Promises;

  friend class thread_pool;

  public:
  future<T> get_future() const
  {
    return future<T>(m_shared_state);
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

    m_shared_state->on_ready();
  }

  template <typename ExcpetionType>
    requires(!std::same_as<ExcpetionType, std::exception_ptr>)
  void set_exception(const ExcpetionType& t_exception)
  {
    set_exception(std::make_exception_ptr(t_exception));
  }

  void set_exception(const std::exception_ptr& t_exception_ptr)
  {
    assert(t_exception_ptr != nullptr);

    std::lock_guard lock(m_shared_state->m_mutex);
    if(m_shared_state->m_is_ready)
    {
      throw std::runtime_error("promise is already ready");
    }

    m_shared_state->m_exception = t_exception_ptr;
    m_shared_state->m_is_ready  = true;

    m_shared_state->on_ready();
  }

  private:
  std::shared_ptr<detail::state<result_type>> m_shared_state;
};

template <typename T>
promise<T> make_promise() noexcept
{
  return promise<T>();
};

#ifdef WITH_STD_EXPECTED

template <typename T, typename E>
expected::promise<T, E> make_promise() noexcept
{
  return expected::make_promise<T, E>();
}
#endif
} // namespace plz::async
#endif // __FUTURE_H__