#ifndef __EXPECTED_FUTURES_H__
#define __EXPECTED_FUTURES_H__


#ifdef WITH_STD_EXPECTED

#include <algorithm>
#include <format>
#include <mutex>
#include <ranges>
#include <variant>

#include "plz/help/type_traits.hpp"

#include "future.hpp"

namespace plz::async
{
class thread_pool;
}

namespace plz::async::expected
{

template <typename T, typename E, std::ranges::range KeysRange = std::vector<T>>
class futures
{
  friend class plz::async::thread_pool;

  public:
  using key_type    = std::ranges::range_value_t<KeysRange>;
  using result_type = T;
  using error_type  = E;

  using aggregate_result_type = std::vector<T>;
  using aggregate_error_type  = error_type;

  using result_variant = std::variant<std::monostate, result_type, error_type>;
  using futures_map_type = std::vector<std::pair<key_type, future<result_type, error_type>>>; // TODO use flat_map

  struct future_element
  {
    size_t index;
    key_type key;
    future<result_type, error_type> future;
    result_variant result;
  };

  private:
  class promises
  {
    friend class futures<result_type, error_type, KeysRange>;

    promise<aggregate_result_type, error_type>* m_aggregate_promise;
    std::vector<future_element> m_futures;
    std::mutex m_mutex;
    size_t m_ready_count{ 0 };

    public:
    ~promises()
    {
      std::lock_guard guard{ m_mutex };
    }

    promises(futures_map_type futures, promise<aggregate_result_type, error_type>* aggregate_promise)
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
          .on_error(
            [this, index](const error_type& error)
            {
              handle_future_ready(index, error);
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
        auto has_error = std::ranges::find_if(m_futures,
          [](const auto& element)
          {
            return std::holds_alternative<error_type>(element.result);
          });

        if(has_error == m_futures.cend())
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
          m_aggregate_promise->set_error(std::get<error_type>(has_error->result));
        }
      }
    }
  };

  promise<aggregate_result_type, error_type> m_aggregate_promise;
  std::shared_ptr<promises> m_promises;

  public:
  [[nodiscard]] futures(futures_map_type futures, thread_pool* pool = nullptr)

    : m_aggregate_promise{ make_promise<aggregate_result_type, aggregate_error_type>() },
      m_promises{ std::make_shared<promises>(std::move(futures), &m_aggregate_promise) }
  {
    // capture promises by a copy that share ownership in order to make sure m_promises lives at least until the aggregate promise is fulfilled
    m_aggregate_promise.get_future().then(
      [impl = m_promises](const auto&)
      {
      });
  }

  ~futures()
  {
  }

  public:
  future<result_type, error_type>& get_future(const key_type& key)
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

  future<result_type, error_type>& get_future(const size_t& index)
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

  std::expected<result_type, error_type> wait(const size_t& index) const
  {
    return get_future(index).wait();
  }

  std::expected<result_type, error_type> wait(const key_type& key) const
  {
    return get_future(key).wait();
  }

  std::expected<aggregate_result_type, aggregate_error_type> wait() const
  {
    return get_aggregate_future().wait();
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
  auto on_error(Func&& t_error_handler)
  {
    auto aggregate_future = get_aggregate_future();
    aggregate_future.on_error(std::forward<Func>(t_error_handler));
    return aggregate_future;
  }

  future<aggregate_result_type, aggregate_error_type> get_aggregate_future() const
  {
    return m_aggregate_promise.get_future();
  }
};
} // namespace plz::async::expected

#endif // WITH_STD_EXPECTED
#endif // __EXPECTED_FUTURES_H__