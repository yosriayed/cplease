#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <ranges>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

#include "plz/help/callable.hpp"
#include "plz/help/type_traits.hpp"

#include "futures.hpp"
#include "packaged_task.hpp"

namespace plz
{

void set_threads_count(int num_threads);

template <typename Func, typename... Args>
auto run(Func&& function, Args&&... args);

template <std::ranges::range Range, typename Func, typename... Args>
auto map(Range&& range, Func&& function, Args&&... args);

void wait();

template <class Rep, class Period>
void wait_for(const std::chrono::duration<Rep, Period>& timeout);

void quit();

class thread_pool
{
  public:
  using task_type_st = plz::callable<void(std::stop_token)>;
  using task_type    = plz::callable<void()>;
  using task_variant = std::variant<task_type, task_type_st>;

  public:
  static void set_global_instance_thread_count(int num_threads)
  {
    s_count_threads_global_instance = num_threads;

    if(s_global_instance_initialized)
    {
      throw std::runtime_error("global instance already initialized");
    }
  }

  static thread_pool& global_instance()
  {
    static thread_pool instance(s_count_threads_global_instance);
    s_global_instance_initialized = true;
    return instance;
  }

  thread_pool(size_t num_threads = std::thread::hardware_concurrency())
  {
    for(size_t i = 0; i < num_threads; ++i)
    {
      m_threads.emplace_back(std::bind_front(&thread_pool::thread_work, this));
    }
  }

  ~thread_pool()
  {
    quit();
  }

  void run(task_variant&& task)
  {
    {
      std::lock_guard lock(m_mutex);

      if(m_stop)
      {
        throw std::runtime_error("enqueue on stopped thread_pool");
      }

      m_tasks.push(std::move(task));
    }

    m_workers_wait_condition.notify_one();
  }

  template <typename Func, typename... Args>
    requires std::invocable<Func, Args...>
  auto run(Func&& function, Args&&... args) -> future<std::invoke_result_t<Func, Args...>>
  {
    packaged_task<Func, Args...> task{ std::forward<Func>(function),
      std::forward<Args>(args)... };

    auto future                           = task.get_future();
    task.m_promise.m_shared_state->m_pool = this;

    run(task_variant(task_type::from(std::move(task))));

    return future;
  }

  template <typename Func, typename... Args>
    requires std::invocable<Func, Args..., std::stop_token>
  auto run(Func&& function,
    Args&&... args) -> future<std::invoke_result_t<Func, Args..., std::stop_token>>
  {
    packaged_task_st<Func, Args...> task{ std::forward<Func>(function),
      std::forward<Args>(args)... };

    auto future                           = task.get_future();
    task.m_promise.m_shared_state->m_pool = this;

    run(task_variant(task_type_st(std::move(task))));

    return future;
  }

  template <std::ranges::range Range, typename Func, typename... Args>
    requires std::invocable<Func, std::ranges::range_value_t<Range>, Args...>
  auto map(Range&& range, Func&& function, Args&&... args)
    -> futures<std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>, std::ranges::range_value_t<Range>>
  {
    using KeyType = std::ranges::range_value_t<Range>;

    using func_return_type =
      typename std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>;

    using FuturesMapType = typename futures<func_return_type, KeyType>::futures_map_type;

    std::lock_guard lock(m_mutex);
    FuturesMapType futuresMap;

    if(!m_stop)
    {
      for(auto&& v : range)
      {
        packaged_task task{ std::forward<Func>(function),
          std::forward<KeyType>(v),
          std::forward<Args>(args)... };

        auto future                           = task.get_future();
        task.m_promise.m_shared_state->m_pool = this;

        m_tasks.emplace(task_type::from(std::move(task)));

        futuresMap.push_back({ v, std::move(future) });
      }

      futures<func_return_type, KeyType> futures(std::move(futuresMap));
      futures.m_aggregate_promise.m_shared_state->m_pool = this;

      auto size = std::ranges::size(range);

      for(auto i = 0; i < size; ++i)
      {
        m_workers_wait_condition.notify_one();
      }

      return futures;
    }
    else
    {
      throw std::runtime_error("enqueue on stopped thread_pool");
    }
  }

  template <std::ranges::range Range, typename Func, typename... Args>
    requires std::invocable<Func, std::ranges::range_value_t<Range>, Args..., std::stop_token>
  auto map(Range&& range, Func&& function, Args&&... args)
    -> futures<std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args..., std::stop_token>,
      std::ranges::range_value_t<Range>>
  {
    using KeyType = std::ranges::range_value_t<Range>;

    using func_return_type =
      typename std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args..., std::stop_token>;

    using FuturesMapType = typename futures<func_return_type, KeyType>::futures_map_type;

    std::lock_guard lock(m_mutex);
    FuturesMapType futuresMap;

    if(!m_stop)
    {
      for(auto&& v : range)
      {
        packaged_task_st task{ std::forward<Func>(function),
          std::forward<KeyType>(v),
          std::forward<Args>(args)... };

        auto future                           = task.get_future();
        task.m_promise.m_shared_state->m_pool = this;

        m_tasks.emplace(task_type_st::from(std::move(task)));

        futuresMap.push_back({ v, std::move(future) });
      }

      futures<func_return_type, KeyType> futures(std::move(futuresMap));
      futures.m_aggregate_promise.m_shared_state->m_pool = this;

      auto size = std::ranges::size(range);

      for(auto i = 0; i < size; ++i)
      {
        m_workers_wait_condition.notify_one();
      }

      return futures;
    }
    else
    {
      throw std::runtime_error("enqueue on stopped thread_pool");
    }
  }

  void wait()
  {
    std::unique_lock lock{ m_mutex };

    while(!is_idle())
    {
      m_pool_wait_condition.wait(lock,
        [this]()
        {
          return is_idle();
        });
    }
  }

  template <class Rep, class Period>
  void wait_for(const std::chrono::duration<Rep, Period>& timeout)
  {
    std::unique_lock lock{ m_mutex };

    while(!is_idle())
    {
      m_pool_wait_condition.wait_for(lock,
        timeout,
        [this]()
        {
          return is_idle();
        });
    }
  }

  void quit()
  {
    {
      std::lock_guard lock(m_mutex);

      if(m_stop)
      {
        return;
      }

      m_stop = true;
    }

    m_workers_wait_condition.notify_all();

    for(auto& th : m_threads)
    {
      th.request_stop();
      th.join();
    }
  }

  private:
  void thread_work(std::stop_token stop_token)
  {
    while(!m_stop)
    {
      task_variant task;
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_workers_wait_condition.wait(lock,
          [this]
          {
            return m_stop || !m_tasks.empty();
          });

        if(m_stop && m_tasks.empty())
        {
          return;
        }

        task = std::move(m_tasks.front());
        m_tasks.pop();
      }

      ++m_busy_count;

      if(std::holds_alternative<task_type>(task))
      {
        std::get<task_type>(task)();
      }
      else
      {
        std::get<task_type_st>(task)(stop_token);
      }

      --m_busy_count;

      if(m_busy_count == 0)
      {
        m_pool_wait_condition.notify_all();
      }
    }
  }

  bool is_idle() const
  {
    return m_tasks.empty() && (m_busy_count == 0);
  }

  std::vector<std::jthread> m_threads;
  std::queue<task_variant> m_tasks;

  std::mutex m_mutex;
  std::atomic<size_t> m_busy_count{ 0 };
  std::condition_variable m_workers_wait_condition;
  std::condition_variable m_pool_wait_condition;

  static inline int s_count_threads_global_instance = std::thread::hardware_concurrency();
  static inline int s_global_instance_initialized = false;

  bool m_stop{ false };
};

template <typename Func, typename... Args>
auto run(Func&& function, Args&&... args)
{
  return thread_pool::global_instance().run(
    std::forward<Func>(function), std::forward<Args>(args)...);
}

template <std::ranges::range Range, typename Func, typename... Args>
auto map(Range&& range, Func&& function, Args&&... args)
{
  return thread_pool::global_instance().map(
    std::forward<Range>(range), std::forward<Func>(function), std::forward<Args>(args)...);
}

inline void wait()
{
  thread_pool::global_instance().wait();
}

template <class Rep, class Period>
void wait_for(const std::chrono::duration<Rep, Period>& timeout)
{
  thread_pool::global_instance().wait_for(timeout);
}

inline void quit()
{
  thread_pool::global_instance().quit();
}

inline void set_threads_count(int num_threads)
{
  thread_pool::set_global_instance_thread_count(num_threads);
}

template <typename T>
template <typename Func, typename... Args>
auto future<T>::then(thread_pool* pool, Func&& func, Args&&... args)
{
  return then(
    [pool, func = std::forward<Func>(func), ... args = std::forward<Args>(args)](const auto& value)
    {
      return pool->run(func, value, args...);
    });
}

} // namespace plz

#endif // __THREAD_POOL_H__