#ifndef __CHANNEL_H__
#define __CHANNEL_H__

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "plz/help/array_traits.hpp"
#include "plz/thread_pool.hpp"

#include "concepts.hpp"
#include "reader.hpp"
#include "writer.hpp"

namespace plz
{

using namespace plz::circbuff;

template <circular_buffer_ptr BufferPointer>
class source;

template <circular_buffer_ptr BufferPointer>
class sink;

template <size_t SINKS_COUNT>
constexpr auto make_spmc_channel(circular_buffer_ptr auto buffer_ptr)
  -> std::pair<source<decltype(buffer_ptr)>, std::array<sink<decltype(buffer_ptr)>, SINKS_COUNT>>;

template <size_t SOURCES_COUNT>
constexpr auto make_mpsc_channel(circular_buffer_ptr auto buffer_ptr)
  -> std::pair<std::array<source<decltype(buffer_ptr)>, SOURCES_COUNT>, sink<decltype(buffer_ptr)>>;

constexpr auto make_channel(circular_buffer_ptr auto buffer_ptr)
  -> std::pair<source<decltype(buffer_ptr)>, sink<decltype(buffer_ptr)>>;

using source_notify_function_id = size_t;

struct source_connection
{
  source_notify_function_id id;
};

template <circular_buffer_ptr BufferPointer, typename Func>
source_connection
connect(source<BufferPointer>* source, sink<BufferPointer>* sink, Func&& func);

template <circular_buffer_ptr BufferPointer, typename Func>
source_connection connect(source<BufferPointer>* source,
  sink<BufferPointer>* sink,
  Func&& func,
  plz::thread_pool* pool);

template <circular_buffer_ptr BufferPointer, typename Func>
source_connection
async_connect(source<BufferPointer>* source, sink<BufferPointer>* sink, Func&& func);

template <circular_buffer_ptr BufferPointer>
bool disconnect(source<BufferPointer>* source, source_connection connection);

namespace detail
{
template <circular_buffer_ptr BufferPointer>
class channel
{
  public:
  using buffer_ptr_type = BufferPointer;
  using array_type = typename std::pointer_traits<BufferPointer>::element_type;
  using value_type =
    typename array_traits<typename std::pointer_traits<BufferPointer>::element_type>::value_type;

  static constexpr size_t CAPACITY = array_traits<array_type>::capacity;

  private:
  buffer_ptr_type m_buffer;
  writer<buffer_ptr_type> m_writer;

  template <size_t SINKS_COUNT>
  friend constexpr auto make_spmc_channel(circular_buffer_ptr auto buffer_ptr)
    -> std::pair<source<decltype(buffer_ptr)>, std::array<sink<decltype(buffer_ptr)>, SINKS_COUNT>>;

  template <size_t SOURCES_COUNT>
  friend constexpr auto make_mpsc_channel(circular_buffer_ptr auto buffer_ptr)
    -> std::pair<std::array<source<decltype(buffer_ptr)>, SOURCES_COUNT>, sink<decltype(buffer_ptr)>>;

  friend constexpr auto make_channel(circular_buffer_ptr auto buffer_ptr)
    -> std::pair<source<decltype(buffer_ptr)>, sink<decltype(buffer_ptr)>>;

  friend class source<buffer_ptr_type>;
  friend class sink<buffer_ptr_type>;

  public:
  channel(BufferPointer buffer_ptr)
    : m_buffer{ std::move(buffer_ptr) }, m_writer{ m_buffer }
  {
  }
};

} // namespace detail

template <circular_buffer_ptr BufferPointer>
class source
{
  public:
  using buffer_ptr_type = BufferPointer;
  using array_type = typename std::pointer_traits<buffer_ptr_type>::element_type;
  using value_type =
    typename array_traits<typename std::pointer_traits<buffer_ptr_type>::element_type>::value_type;

  static constexpr size_t CAPACITY = array_traits<array_type>::capacity;

  using notify_function = std::function<void(size_t)>;

  private:
  std::atomic<source_notify_function_id> m_notify_function_id_counter{ 0 };

  std::shared_ptr<detail::channel<buffer_ptr_type>> m_channel;
  std::vector<std::pair<source_notify_function_id, notify_function>> m_notif_funcs;

  public:
  source(std::shared_ptr<detail::channel<buffer_ptr_type>> channel)
    : m_channel{ std::move(channel) }
  {
  }

  source clone() const
  {
    return source(*this);
  }

  source() = default;

  source(const source& other) : m_channel{ other.m_channel }
  {
  }

  source& operator=(const source& other)
  {
    m_channel = other.m_channel;
    return *this;
  }

  source(source&& other)
  {
    std::swap(m_channel, other.m_channel);
  }

  source& operator=(source&& other)
  {
    std::swap(m_channel, other.m_channel);
    return *this;
  }

  public:
  size_t get_buffer_capacity() const
  {
    return m_channel->m_buffer->size();
  }

  void put(const value_type& value)
  {
    m_channel->m_writer.put(value);
    for(auto& [_, func] : m_notif_funcs)
    {
      func(1);
    }
  }

  void write(const value_type* values, size_t count)
  {
    m_channel->m_writer.write(values, count);

    for(auto& [_, func] : m_notif_funcs)
    {
      func(count);
    }
  };

  template <typename Func>
    requires std::invocable<Func, value_type*, size_t> &&
    std::same_as<std::invoke_result_t<Func, value_type*, size_t>, size_t>
  size_t write_using(Func&& func, size_t count)
  {
    auto size_written = m_channel->m_writer.write_using(std::forward<Func>(func), count);

    for(auto& func : m_notif_funcs)
    {
      func(size_written);
    }

    return size_written;
  };

  template <typename Func>
  source_notify_function_id register_notify_function(Func&& func)
  {
    auto id = m_notify_function_id_counter++;
    m_notif_funcs.emplace_back({ id, std::move(func) });
    return id;
  }

  bool unregister_notify_function(source_notify_function_id id)
  {
    return (std::erase_if(m_notif_funcs,
             [id](auto& pair)
             {
               return pair.first == id;
             })) > 0;
  }
};

template <circular_buffer_ptr BufferPointer>
class sink
{
  public:
  using buffer_pointer_type = BufferPointer;
  using array_type = typename std::pointer_traits<buffer_pointer_type>::element_type;
  using value_type =
    typename array_traits<typename std::pointer_traits<buffer_pointer_type>::element_type>::value_type;

  static constexpr size_t CAPACITY = array_traits<array_type>::capacity;

  private:
  std::shared_ptr<detail::channel<buffer_pointer_type>> m_channel;
  reader<buffer_pointer_type> m_reader;

  public:
  sink(std::shared_ptr<detail::channel<buffer_pointer_type>> channel)
    : m_channel{ std::move(channel) }, m_reader{ m_channel->m_buffer }
  {
  }

  sink clone() const
  {
    return sink(*this);
  }

  void reset()
  {
    m_reader.reset();
  }

  size_t get_buffer_capacity() const
  {
    return m_channel->m_buffer->size();
  }

  size_t get_available_data_size() const
  {
    return std::min(m_channel->m_writer.get_index() - m_reader.get_index(),
      m_channel->m_buffer->size());
  }

  value_type get()
  {
    return m_reader.get();
  }

  const value_type& peek() const
  {
    return m_reader.peek();
  }

  void peek(value_type* values, size_t count) const
  {
    m_reader.peek(values, count);
  }

  size_t read(value_type* values, size_t count)
  {
    auto read_count = std::min(get_available_data_size(), count);
    m_reader.read(values, read_count);
    return read_count;
  }

  template <typename Func>
    requires std::invocable<Func, value_type*, size_t> &&
    std::same_as<std::invoke_result_t<Func, value_type*, size_t>, size_t>
  size_t read_using(Func&& func, size_t count)
  {
    return m_reader.read_using(
      std::forward<Func>(func), std::min(get_available_data_size(), count));
  }

  std::vector<value_type> read(size_t count)
  {
    return m_reader.read(std::min(get_available_data_size(), count));
  }

  std::vector<value_type> read_all()
  {
    std::vector<value_type> values(get_available_data_size());
    read(values.data(), values.size());
    return values;
  }
};

template <size_t SINKS_COUNT>
constexpr auto make_spmc_channel(circular_buffer_ptr auto buffer_ptr)
  -> std::pair<source<decltype(buffer_ptr)>, std::array<sink<decltype(buffer_ptr)>, SINKS_COUNT>>
{
  using buffer_ptr_type = decltype(buffer_ptr);

  auto channel =
    std::make_shared<detail::channel<buffer_ptr_type>>(std::move(buffer_ptr));

  return { source<buffer_ptr_type>(channel),
    plz::make_array_in_place<SINKS_COUNT, sink<buffer_ptr_type>>(channel) };
}

template <size_t SOURCES_COUNT>
constexpr auto make_mpsc_channel(circular_buffer_ptr auto buffer_ptr)
  -> std::pair<std::array<source<decltype(buffer_ptr)>, SOURCES_COUNT>, sink<decltype(buffer_ptr)>>
{
  using buffer_ptr_type = decltype(buffer_ptr);

  auto channel =
    std::make_shared<detail::channel<buffer_ptr_type>>(std::move(buffer_ptr));

  return { plz::make_array_in_place<SOURCES_COUNT, source<buffer_ptr_type>>(channel),
    sink<buffer_ptr_type>(channel) };
}

constexpr auto make_channel(circular_buffer_ptr auto buffer_ptr)
  -> std::pair<source<decltype(buffer_ptr)>, sink<decltype(buffer_ptr)>>
{
  using buffer_ptr_type = decltype(buffer_ptr);
  auto channel =
    std::make_shared<detail::channel<buffer_ptr_type>>(std::move(buffer_ptr));

  return { source<buffer_ptr_type>(channel), sink<buffer_ptr_type>(channel) };
}

template <circular_buffer_ptr BufferPointer, typename Func>
source_connection
connect(source<BufferPointer>* source, sink<BufferPointer>* sink, Func&& func)
{
  return source_connection{ source->register_notify_function(
    [sink, func = std::forward<Func>(func)](size_t count) mutable
    {
      sink->read_using(std::forward<Func>(func), count);
    }) };
}

template <circular_buffer_ptr BufferPointer, typename Func>
source_connection
connect(source<BufferPointer>* source, sink<BufferPointer>* sink, Func&& func, plz::thread_pool* pool)
{
  return source_connection{ source->register_notify_function(
    [sink, func = std::forward<Func>(func), pool](size_t count) mutable
    {
      pool->run(
        [func = std::forward<Func>(func), sink](size_t count) mutable
        {
          sink->read_using(func, count);
        },
        count);
    }) };
}

template <circular_buffer_ptr BufferPointer, typename Func>
source_connection
async_connect(source<BufferPointer>* source, sink<BufferPointer>* sink, Func&& func)
{
  return connect(
    source, sink, std::forward<Func>(func), &plz::thread_pool::global_instance());
}

template <circular_buffer_ptr BufferPointer>
bool disconnect(source<BufferPointer>* source, source_connection connection)
{
  return source->unregister_notify_function(connection.id);
}

} // namespace plz
#endif // __CHANNEL_H__