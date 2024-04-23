#ifndef __FUTURES_H__
#define __FUTURES_H__

#include <algorithm>
#include <format>
#include <ranges>
#include <type_traits>
#include <variant>

#include "plz/help/type_traits.hpp"

#include "future.hpp"

namespace plz
{

class thread_pool;

template <typename T, typename key>
class futures;

template <typename T, typename Key>
futures<T, Key> make_futures(typename futures<T, Key>::promises_map_type futures);

template <typename T, typename Key>
futures<T, Key> make_futures(typename futures<T, Key>::futures_map_type futures);

template <typename T, typename Key>
futures<T, Key> make_futures();

template <std::ranges::range PromisesRange>
  requires(plz::is_specialization_of_v<std::ranges::range_value_t<PromisesRange>, promise>)
futures<typename plz::get_template_arg_type_of<std::ranges::range_value_t<PromisesRange>>::template arg<0>::type, size_t>
make_futures(PromisesRange&& promises);

template <std::ranges::range FuturesRange>
  requires(plz::is_specialization_of_v<std::ranges::range_value_t<FuturesRange>, future>)
futures<typename plz::get_template_arg_type_of<std::ranges::range_value_t<FuturesRange>>::template arg<0>::type, size_t>
make_futures(FuturesRange&& futures);

template <typename T, typename Key>
class futures
{
  friend class thread_pool;

  public:
  using key_type              = Key;
  using result_type           = T;
  using aggregate_result_type = std::vector<T>;
  using result_variant = std::variant<std::monostate, result_type, std::exception_ptr>;
  using futures_map_type = std::vector<std::pair<key_type, future<result_type>>>; // TODO use flat_map
  using promises_map_type = std::vector<std::pair<key_type, promise<result_type>>>; // TODO use flat_map

  struct future_element
  {
    size_t index;
    key_type key;
    future<result_type> future;
    result_variant result;
  };

  private:
  class state
  {
    friend class futures<T, Key>;

    promise<aggregate_result_type>* m_aggregate_promise;
    std::vector<future_element> m_futures;
    std::mutex m_mutex;
    size_t m_ready_count{ 0 };

    public:
    ~state()
    {
      std::lock_guard guard{ m_mutex };
    }

    state(futures_map_type futures, promise<aggregate_result_type>* aggregate_promise)
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

    void add_future(const key_type& key, future<result_type> future)
    {
      std::lock_guard guard{ m_mutex };

      if(m_futures.size() > 0 && m_ready_count == m_futures.size())
      {
        throw std::runtime_error("All promises are already ready");
      }

      future
        .then(
          [this, index = m_futures.size()](const auto& res)
          {
            handle_future_ready(index, res);
          })
        .on_exception(
          [this, index = m_futures.size()](const std::exception_ptr& exception)
          {
            handle_future_ready(index, exception);
          });

      m_futures.emplace_back(m_futures.size(), key, std::move(future), std::monostate());
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
  std::shared_ptr<state> m_state;

  public:
  futures(const promises_map_type& promises_map)
    : m_aggregate_promise{ make_promise<aggregate_result_type>() }
  {
    futures_map_type futures;
    for(auto&& [key, promise] : promises_map)
    {
      futures.push_back({ key, promise.get_future() });
    }

    m_state = std::make_shared<state>(std::move(futures), &m_aggregate_promise);
  }
  futures(futures_map_type futures)

    : m_aggregate_promise{ make_promise<aggregate_result_type>() },
      m_state{ std::make_shared<state>(std::move(futures), &m_aggregate_promise) }
  {
    // capture promises by a copy that share ownership in order to make sure m_promises lives at least until the aggregate promise is fulfilled
    m_aggregate_promise.get_future().then(
      [impl = m_state](const auto&)
      {
      });
  }

  futures() : futures(futures_map_type{})
  {
  }

  void add_promise(const key_type& key, const promise<result_type>& promise)
  {
    m_state->add_future(key, promise.get_future());
  }

  void add_future(const key_type& key, future<result_type> future)
  {
    m_state->add_future(key, std::move(future));
  }

  future<result_type>& get_future(const key_type& key) const
  {
    auto it = std::find_if(m_state->m_futures.begin(),
      m_state->m_futures.end(),
      [&key](const auto& p)
      {
        return p.key == key;
      });

    if(it != m_state->m_futures.end())
    {
      return it->future;
    }
    else
    {
      throw std::runtime_error(std::format("No promise with key {} exists", key));
    }
  }

  future<result_type>& get_future(const size_t& index) const
    requires(!std::is_integral_v<key_type>)
  {
    if(index < m_state->m_futures.size())
    {
      return m_state->m_futures[index].future;
    }
    else
    {
      throw std::runtime_error(std::format("No promise with index {} exists", index));
    }
  }

  future<result_type>& get_future_by_index(const size_t& index)
  {
    if(index < m_state->m_futures.size())
    {
      return m_state->m_futures[index].future;
    }
    else
    {
      throw std::runtime_error(std::format("No promise with index {} exists", index));
    }
  }

  result_type get(const size_t& index) const
    requires(!std::is_integral_v<key_type>)
  {
    return get_future(index).get();
  }

  result_type take(const size_t& index) const
    requires(!std::is_integral_v<key_type>)
  {
    return get_future(index).take();
  }

  result_type get_by_index(const size_t& index) const
  {
    return get_future(index).get();
  }

  result_type take_by_index(const size_t& index) const
  {
    return get_future(index).take();
  }

  result_type get(const key_type& key) const
  {
    return get_future(key).get();
  }

  aggregate_result_type take(const key_type& key) const
  {
    return get_future(key).take();
  }

  aggregate_result_type get() const
  {
    return get_aggregate_future().get();
  }

  aggregate_result_type take() const
  {
    return get_aggregate_future().take();
  }

  template <typename Func, typename... Args>
    requires std::same_as<std::invoke_result_t<Func, aggregate_result_type, Args...>, void>
  auto then(Func&& func, Args&&... args)
  {
    auto aggregate_future = get_aggregate_future();
    aggregate_future.then(std::forward<Func>(func), std::forward<Args>(args)...);
    return aggregate_future;
  }

  template <typename Func, typename... Args>
  auto then(Func&& func, Args&&... args)
  {
    return get_aggregate_future().then(
      std::forward<Func>(func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  auto then(thread_pool* pool, Func&& func, Args&&... args)
  {
    return get_aggregate_future().then(
      pool, std::forward<Func>(func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  auto async_then(Func&& func, Args&&... args)
  {
    return get_aggregate_future().async_then(
      std::forward<Func>(func), std::forward<Args>(args)...);
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

template <typename T, typename Key>
futures<T, Key> make_futures(typename futures<T, Key>::futures_map_type futures_map)
{
  return futures<T, Key>(std::move(futures_map));
}

template <typename T, typename Key>
futures<T, Key> make_futures()
{
  return futures<T, Key>();
}

template <typename T, typename Key>
futures<T, Key> make_futures(typename futures<T, Key>::promises_map_type promises_map)
{
  return futures<T, Key>(std::move(promises_map));
}

template <std::ranges::range PromisesRange>
  requires(plz::is_specialization_of_v<std::ranges::range_value_t<PromisesRange>, promise>)
futures<typename plz::get_template_arg_type_of<std::ranges::range_value_t<PromisesRange>>::template arg<0>::type, size_t>
make_futures(PromisesRange&& promises)
{
  using T =
    typename plz::get_template_arg_type_of<std::ranges::range_value_t<PromisesRange>>::template arg<0>::type;
  using Key = size_t;
  typename plz::futures<T, Key>::promises_map_type promises_map;
  std::ranges::transform(promises,
    std::back_inserter(promises_map),
    [&promises_map](auto&& promise)
    {
      return std::pair{ promises_map.size(), promise };
    });

  return make_futures<T, Key>(promises_map);
}

template <std::ranges::range FuturesRange>
  requires(plz::is_specialization_of_v<std::ranges::range_value_t<FuturesRange>, future>)
futures<typename plz::get_template_arg_type_of<std::ranges::range_value_t<FuturesRange>>::template arg<0>::type, size_t>
make_futures(FuturesRange&& futures)
{
  using T =
    typename plz::get_template_arg_type_of<std::ranges::range_value_t<FuturesRange>>::template arg<0>::type;
  using Key = size_t;
  typename plz::futures<T, Key>::futures_map_type futures_map;
  std::ranges::transform(futures,
    std::back_inserter(futures_map),
    [&futures_map](auto&& future)
    {
      return std::pair{ futures_map.size(), future };
    });

  return make_futures<T, Key>(futures_map);
}

} // namespace plz

#endif // __FUTURES_H__