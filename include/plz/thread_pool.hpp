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
#include <vector>

#include "plz/help/type_traits.hpp"

#include "futures.hpp"
#include "packaged_task.hpp"

#ifdef WITH_STD_EXPECTED
#include "expected/futures.hpp"
#include "expected/packaged_task.hpp"
#endif

namespace plz
{

void set_threads_count(int num_threads);

template <typename Func, typename... Args>
auto run(Func&& function, Args&&... t_args);

template <std::ranges::range Range, typename Func, typename... Args>
auto map(Range&& t_range, Func&& function, Args&&... t_args);

void wait();

template <class Rep, class Period>
void wait_for(const std::chrono::duration<Rep, Period>& timeout);

void quit();

class thread_pool
{
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

  thread_pool(size_t t_num_threads = std::thread::hardware_concurrency())
  {
    for(size_t i = 0; i < t_num_threads; ++i)
    {
      m_threads.emplace_back(std::bind_front(&thread_pool::thread_work, this));
    }
  }

  ~thread_pool()
  {
    quit();
  }

  void run(std::unique_ptr<packaged_task_base> task)
  {
    {
      std::lock_guard lock(m_mutex);

      if(m_stop)
      {
        throw std::runtime_error("enqueue on stopped thread_pool");
      }

      m_tasks.emplace(std::move(task));
    }

    m_workers_wait_condition.notify_one();
  }

  template <typename Func, typename... Args>
    requires std::invocable<Func, Args...>
  auto run(Func&& function, Args&&... t_args)
    -> future<std::invoke_result_t<Func, Args...>>
  {
    auto task = std::make_unique<plz::packaged_task<Func, Args...>>(
      std::forward<Func>(function), std::forward<Args>(t_args)...);

    auto future                            = task->get_future();
    task->m_promise.m_shared_state->m_pool = this;

    {
      std::lock_guard lock(m_mutex);

      if(m_stop)
      {
        throw std::runtime_error("enqueue on stopped thread_pool");
      }

      m_tasks.emplace(std::move(task));
    }

    m_workers_wait_condition.notify_one();

    return future;
  }

  template <typename Func, typename... Args>
    requires std::invocable<Func, Args..., std::stop_token>
  auto run(Func&& function, Args&&... t_args)
    -> future<std::invoke_result_t<Func, Args..., std::stop_token>>
  {
    auto task = std::make_unique<packaged_task_st<Func, Args...>>(
      std::forward<Func>(function), std::forward<Args>(t_args)...);

    auto future                            = task->get_future();
    task->m_promise.m_shared_state->m_pool = this;

    {
      std::lock_guard lock(m_mutex);

      if(m_stop)
      {
        throw std::runtime_error("enqueue on stopped thread_pool");
      }

      m_tasks.emplace(std::move(task));
    }

    m_workers_wait_condition.notify_one();

    return future;
  }

#ifdef WITH_STD_EXPECTED
  template <typename Func, typename... Args>
    requires std::invocable<Func, Args...> &&
    plz::specialization_of<std::invoke_result_t<Func, Args...>, std::expected>
  auto run(Func&& function, Args&&... t_args)
    -> expected::future<typename std::invoke_result_t<Func, Args...>::value_type,
      typename std::invoke_result_t<Func, Args...>::error_type>
  {
    auto task = std::make_unique<expected::packaged_task<Func, Args...>>(
      std::forward<Func>(function), std::forward<Args>(t_args)...);

    auto future                            = task->get_future();
    task->m_promise.m_shared_state->m_pool = this;

    {
      std::lock_guard lock(m_mutex);

      if(m_stop)
      {
        throw std::runtime_error("enqueue on stopped thread_pool");
      }

      m_tasks.emplace(std::move(task));
    }

    m_workers_wait_condition.notify_one();

    return future;
  }

  template <typename Func, typename... Args>
    requires std::invocable<Func, Args..., std::stop_token> &&
    plz::specialization_of<std::invoke_result_t<Func, Args..., std::stop_token>, std::expected>
  auto run(Func&& function, Args&&... t_args)
    -> expected::future<typename std::invoke_result_t<Func, Args..., std::stop_token>::value_type,
      typename std::invoke_result_t<Func, Args..., std::stop_token>::error_type>
  {
    auto task = std::make_unique<expected::packaged_task_st<Func, Args...>>(
      std::forward<Func>(function), std::forward<Args>(t_args)...);

    auto future                            = task->get_future();
    task->m_promise.m_shared_state->m_pool = this;

    {
      std::lock_guard lock(m_mutex);

      if(m_stop)
      {
        throw std::runtime_error("enqueue on stopped thread_pool");
      }

      m_tasks.emplace(std::move(task));
    }

    m_workers_wait_condition.notify_one();

    return future;
  }

  template <std::ranges::range Range, typename Func, typename... Args>
    requires(
      !plz::specialization_of<std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>, std::expected>)
  auto map(Range&& t_range, Func&& function, Args&&... t_args)
    -> futures<std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>, Range>
  {
    using KeyType = std::ranges::range_value_t<Range>;

    using func_return_type =
      typename std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>;

    using FuturesMapType = typename futures<func_return_type, Range>::futures_map_type;

    std::lock_guard lock(m_mutex);
    FuturesMapType futuresMap;

    if(!m_stop)
    {
      for(auto&& v : t_range)
      {
        auto task = std::make_unique<plz::packaged_task<Func, KeyType, Args...>>(
          std::forward<Func>(function),
          std::forward<KeyType>(v),
          std::forward<Args>(t_args)...);

        auto future                            = task->get_future();
        task->m_promise.m_shared_state->m_pool = this;

        m_tasks.emplace(std::move(task));

        futuresMap.push_back({ v, std::move(future) });
      }

      futures<func_return_type, Range> futures(std::move(futuresMap));
      futures.m_aggregate_promise.m_shared_state->m_pool = this;

      auto size = std::ranges::size(t_range);

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
    requires plz::specialization_of<std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>, std::expected>
  auto map(Range&& t_range, Func&& function, Args&&... t_args)
    -> expected::futures<typename std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>::value_type,
      typename std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>::error_type,
      Range>
  {
    using KeyType = std::ranges::range_value_t<Range>;

    using func_return_type =
      typename std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>;

    using result_type = typename func_return_type::value_type;
    using error_type  = typename func_return_type::error_type;

    using FuturesMapType =
      typename expected::futures<result_type, error_type, Range>::futures_map_type;

    std::lock_guard lock(m_mutex);
    FuturesMapType futuresMap;

    if(!m_stop)
    {
      for(auto&& v : t_range)
      {
        auto task = std::make_unique<expected::packaged_task<Func, KeyType, Args...>>(
          std::forward<Func>(function),
          std::forward<KeyType>(v),
          std::forward<Args>(t_args)...);

        auto future                            = task->get_future();
        task->m_promise.m_shared_state->m_pool = this;

        m_tasks.emplace(std::move(task));

        futuresMap.push_back({ v, std::move(future) });
      }

      expected::futures<result_type, error_type, Range> futures(std::move(futuresMap));
      futures.m_aggregate_promise.m_shared_state->m_pool = this;

      auto size = std::ranges::size(t_range);

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
#else

  template <std::ranges::range Range, typename Func, typename... Args>
    requires std::invocable<Func, std::ranges::range_value_t<Range>, Args...>
  auto map(Range&& t_range, Func&& function, Args&&... t_args)
    -> futures<std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>, Range>
  {
    using KeyType = std::ranges::range_value_t<Range>;

    using func_return_type =
      typename std::invoke_result_t<Func, std::ranges::range_value_t<Range>, Args...>;

    using FuturesMapType = typename futures<func_return_type, Range>::futures_map_type;

    std::lock_guard lock(m_mutex);
    FuturesMapType futuresMap;

    if(!m_stop)
    {
      for(auto&& v : t_range)
      {
        auto task = std::make_unique<plz::task<Func, KeyType, Args...>>(
          std::forward<Func>(function),
          std::forward<KeyType>(v),
          std::forward<Args>(t_args)...);

        auto future                            = task->get_future();
        task->m_promise.m_shared_state->m_pool = this;

        m_tasks.emplace(std::move(task));

        futuresMap.push_back({ v, std::move(future) });
      }

      futures<func_return_type, Range> futures(std::move(futuresMap));
      futures.m_aggregate_promise.m_shared_state->m_pool = this;

      auto size = std::ranges::size(t_range);

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

#endif

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
      std::unique_ptr<packaged_task_base> task;
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

      task->run(stop_token);

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
  std::queue<std::unique_ptr<packaged_task_base>> m_tasks;
  std::mutex m_mutex;
  std::atomic<size_t> m_busy_count{ 0 };
  std::condition_variable m_workers_wait_condition;
  std::condition_variable m_pool_wait_condition;

  static inline int s_count_threads_global_instance = std::thread::hardware_concurrency();
  static inline int s_global_instance_initialized = false;

  bool m_stop{ false };
};

template <typename Func, typename... Args>
auto run(Func&& function, Args&&... t_args)
{
  return thread_pool::global_instance().run(
    std::forward<Func>(function), std::forward<Args>(t_args)...);
}

template <std::ranges::range Range, typename Func, typename... Args>
auto map(Range&& t_range, Func&& function, Args&&... t_args)
{
  return thread_pool::global_instance().map(std::forward<Range>(t_range),
    std::forward<Func>(function),
    std::forward<Args>(t_args)...);
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
    [pool, func = std::forward<Func>(func), ... args = std::forward<Args>(args)](
      const auto& value)
    {
      return pool->run(func, value, args...);
    });
}

#ifdef WITH_STD_EXPECTED
template <typename T, typename E>
template <typename Func, typename... Args>
auto expected::future<T, E>::then(thread_pool* pool, Func&& func, Args&&... args)
{
  return then(
    [pool, func = std::forward<Func>(func), ... args = std::forward<Args>(args)](
      const auto& value)
    {
      return pool->run(func, value, args...);
    });
}
#endif

} // namespace plz

#endif // __THREAD_POOL_H__