#ifndef __FUTURES_H__
#define __FUTURES_H__

#include <algorithm>
#include <format>
#include <ranges>
#include <variant>

#include "plz/help/type_traits.hpp"

#include "future.hpp"

namespace plz::async
{

class thread_pool;

template <typename T, std::ranges::range KeysRange = std::vector<T>>
class futures
{
  friend class thread_pool;

  public:
  using key_type              = std::ranges::range_value_t<KeysRange>;
  using result_type           = T;
  using aggregate_result_type = std::vector<T>;
  using result_variant = std::variant<std::monostate, result_type, std::exception_ptr>;
  using futures_map_type = std::vector<std::pair<key_type, future<result_type>>>; // TODO use flat_map

  struct future_element
  {
    size_t index;
    key_type key;
    future<result_type> future;
    result_variant result;
  };

  private:
  class promises
  {
    friend class futures<T, KeysRange>;

    promise<aggregate_result_type>* m_aggregate_promise;
    std::vector<future_element> m_futures;
    std::mutex m_mutex;
    size_t m_ready_count{ 0 };

    public:
    ~promises()
    {
      std::lock_guard guard{ m_mutex };
    }

    promises(futures_map_type futures, promise<aggregate_result_type>* aggregate_promise)
      : m_aggregate_promise{ aggregate_promise }
    {
      size_t index{ 0 };
      for(auto&& [key, future] : futures)
      {
        future
          .then(
            [this, index](const auto& res)
            {
              handle_future_ready(index, res);
            })
          .on_exception(
            [this, index](const std::exception_ptr& exception)
            {
              handle_future_ready(index, exception);
            });

        m_futures.emplace_back(index++, std::move(key), std::move(future), std::monostate());
      }
    }

    private:
    void handle_future_ready(const size_t& index, result_variant result)
    {
      std::lock_guard guard{ m_mutex };

      auto it = std::ranges::find_if(m_futures,
        [index](const auto& element)
        {
          return element.index == index;
        });

      if(it == m_futures.end())
      {
        return;
      }

      it->result = result;
      m_ready_count++;

      if(m_ready_count == m_futures.size())
      {
        auto has_exception_it = std::ranges::find_if(m_futures,
          [](const auto& element)
          {
            return std::holds_alternative<std::exception_ptr>(element.result);
          });

        if(has_exception_it == m_futures.cend())
        {
          aggregate_result_type accumulated_results;
          std::ranges::transform(m_futures,
            std::back_inserter(accumulated_results),
            [](const auto& element)
            {
              return std::get<result_type>(element.result);
            });

          m_aggregate_promise->set_result(accumulated_results);
        }
        else
        {
          m_aggregate_promise->set_exception(
            std::get<std::exception_ptr>(has_exception_it->result));
        }
      }
    }
  };

  promise<aggregate_result_type> m_aggregate_promise;
  std::shared_ptr<promises> m_promises;

  public:
  [[nodiscard]] futures(futures_map_type futures, thread_pool* pool = nullptr)

    : m_aggregate_promise{ make_promise<aggregate_result_type>() },
      m_promises{ std::make_shared<promises>(std::move(futures), &m_aggregate_promise) }
  {
    // capture promises by a copy that share ownership in order to make sure m_promises lives at least until the aggregate promise is fulfilled
    m_aggregate_promise.get_future().then(
      [impl = m_promises](const auto&)
      {
      });
  }

  public:
  future<result_type>& get_future(const key_type& key)
  {
    auto it = std::find_if(m_promises->m_futures.begin(),
      m_promises->m_futures.end(),
      [&key](const auto& p)
      {
        return p.key == key;
      });

    if(it != m_promises->m_futures.end())
    {
      return it->future;
    }
    else
    {
      throw std::runtime_error(std::format("No promise with key {} exists", key));
    }
  }

  future<result_type>& get_future(const size_t& index)
  {
    if(index < m_promises->m_futures.size())
    {
      return m_promises->m_futures[index].future;
    }
    else
    {
      throw std::runtime_error(std::format("No promise with index {} exists", index));
    }
  }

  result_type wait(const size_t& index) const
  {
    return get_future(index).wait();
  }

  result_type take(const size_t& index) const
  {
    return get_future(index).take();
  }

  result_type wait(const key_type& key) const
  {
    return get_future(key).wait();
  }

  aggregate_result_type take(const key_type& key) const
  {
    return get_future(key).take();
  }

  aggregate_result_type wait() const
  {
    return get_aggregate_future().wait();
  }

  aggregate_result_type take() const
  {
    return get_aggregate_future().take();
  }

  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, aggregate_result_type, Args...>, void>
  auto then(Func&& t_func, Args&&... args)
  {
    auto aggregate_future = get_aggregate_future();
    aggregate_future.then(std::forward<Func>(t_func), std::forward<Args>(args)...);
    return aggregate_future;
  }

  template <typename Context, typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, Context&, aggregate_result_type, Args...>, void>
  auto then(Context* t_context, Func&& t_func, Args&&... args)
  {
    auto aggregate_future = get_aggregate_future();
    aggregate_future.then(
      t_context, std::forward<Func>(t_func), std::forward<Args>(args)...);
    return aggregate_future;
  }

  template <typename Func, typename... Args>
  auto then(Func&& t_func, Args&&... args)
  {
    return get_aggregate_future().then(
      std::forward<Func>(t_func), std::forward<Args>(args)...);
  }

  template <typename Context, typename Func, typename... Args>
  auto then(Context* t_context, Func&& t_func, Args&&... args)
  {
    return get_aggregate_future().then(
      t_context, std::forward<Func>(t_func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  auto async_then(thread_pool* t_pool, Func&& t_func, Args&&... args)
  {
    return get_aggregate_future().async_then(
      t_pool, std::forward<Func>(t_func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  auto async_then(Func&& t_func, Args&&... args)
  {
    return get_aggregate_future().async_then(
      std::forward<Func>(t_func), std::forward<Args>(args)...);
  }

  template <typename Func>
  auto on_exception(Func&& t_exception_handler)
  {
    auto aggregate_future = get_aggregate_future();
    aggregate_future.on_exception(std::forward<Func>(t_exception_handler));
    return aggregate_future;
  }

  future<aggregate_result_type> get_aggregate_future() const
  {
    return m_aggregate_promise.get_future();
  }
};

} // namespace plz::async

#endif // __FUTURES_H__