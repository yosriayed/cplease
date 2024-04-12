#ifndef __CIRCBUFF_CIRCULAR_READER_H__
#define __CIRCBUFF_CIRCULAR_READER_H__

#include <atomic>
#include <cassert>
#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

#include "plz/help/type_traits.hpp"

#include "concepts.hpp"

namespace plz::circbuff
{

template <size_t MIN_CONTIGUOUS_SIZE = 0>
constexpr auto make_reader(circular_buffer_ptr auto buffer);

template <circular_buffer_ptr BufferPointer, size_t MIN_CONTIGUOUS_SIZE = 0>
class reader
{
  static constexpr bool g_has_min_contiguous_size = (MIN_CONTIGUOUS_SIZE > 0);
  static constexpr size_t g_min_contiguous_size   = MIN_CONTIGUOUS_SIZE;

  public:
  using buffer_pointer = BufferPointer;
  using array_type = typename std::pointer_traits<BufferPointer>::element_type;
  using value_type =
    typename array_traits<typename std::pointer_traits<BufferPointer>::element_type>::value_type;

  constexpr static size_t get_buffer_capacity()
  {
    return array_traits<array_type>::capacity;
  }

  private:
  buffer_pointer m_buffer;
  std::atomic<size_t> m_index{ 0 };
  std::conditional_t<g_has_min_contiguous_size, std::unique_ptr<std::array<value_type, g_min_contiguous_size>>, void*> m_min_contiguous_buffer =
    nullptr;

  constexpr static size_t g_modmask = get_buffer_capacity() - 1;

  public:
  reader(BufferPointer buffer) : m_buffer{ std::move(buffer) }
  {
    if constexpr(g_has_min_contiguous_size)
    {
      m_min_contiguous_buffer =
        std::make_unique<std::array<value_type, g_min_contiguous_size>>();
    }
  }

  reader(reader&& other) noexcept
    : m_buffer{ std::move(other.m_buffer) },
      m_index{ other.m_index.load() },
      m_min_contiguous_buffer{ std::move(other.m_min_contiguous_buffer) }
  {
  }

  reader& operator=(reader&& other) noexcept
  {
    m_buffer                = std::move(other.m_buffer);
    m_index                 = other.m_index.load();
    m_min_contiguous_buffer = std::move(other.m_min_contiguous_buffer);
    return *this;
  }

  reader(const reader& other)
  {
    m_buffer = other.m_buffer;
    m_index  = other.m_index.load();

    if constexpr(g_has_min_contiguous_size)
    {
      m_min_contiguous_buffer =
        std::make_unique<std::array<value_type, g_min_contiguous_size>>();
    }
  }

  reader& operator=(const reader& other)
  {
    m_buffer = other.m_buffer;
    m_index  = other.m_index.load();

    if constexpr(g_has_min_contiguous_size)
    {
      m_min_contiguous_buffer =
        std::make_unique<std::array<value_type, g_min_contiguous_size>>();
    }

    return *this;
  }

  public:
  size_t get_index() const
  {
    return m_index;
  }

  std::span<const value_type, std::dynamic_extent> get_span_0() const
  {
    return std::span<const value_type>(
      m_buffer->data() + (m_index & g_modmask), get_buffer_capacity());
  }

  std::span<const value_type, std::dynamic_extent> get_span_1() const
  {
    return std::span<const value_type>(
      m_buffer->data(), get_buffer_capacity() - (m_index & g_modmask));
  }

  value_type get()
  {
    value_type value = m_buffer->data()[m_index & g_modmask];
    m_index++;
    return value;
  }

  const value_type& peek() const
  {
    return m_buffer->data()[m_index & g_modmask];
  };

  void peek(value_type* values, size_t count) const
  {
    size_t offset = 0;
    peek_using(
      [&offset, values](value_type* data, size_t size)
      {
        std::memcpy(values + offset, data, size * sizeof(value_type));
        offset += size;
        return size;
      },
      count);
  }

  template <typename Func>
    requires std::invocable<Func, value_type*, size_t> &&
    std::same_as<std::invoke_result_t<Func, value_type*, size_t>, size_t>
  size_t peek_using(Func&& func, size_t count) const
  {
    auto index         = m_index & g_modmask;
    auto size_to_end   = std::min(count, get_buffer_capacity() - index);
    size_t size_peeked = 0;

    size_peeked += func(m_buffer->data() + index, size_to_end);

    if((size_peeked == size_to_end) && (size_peeked < count))
    {
      index              = (m_index + size_peeked) & g_modmask;
      size_t size_read_2 = 0;

      size_peeked += func(m_buffer->data() + index, count - size_peeked);
    }

    return size_peeked;
  }

  std::vector<value_type> peek(size_t count) const
  {
    std::vector<value_type> values(count);

    peek(values.data(), count);

    return values;
  }

  void read(value_type* values, size_t count)
  {
    size_t offset = 0;
    read_using(
      [&offset, values](value_type* data, size_t size)
      {
        std::memcpy(values + offset, data, size * sizeof(value_type));
        offset += size;
        return size;
      },
      count);
  }

  template <typename Func>
    requires std::invocable<Func, value_type*, size_t> &&
    std::same_as<std::invoke_result_t<Func, value_type*, size_t>, size_t>
  size_t read_using(Func&& func, size_t count)
  {
    auto index       = m_index & g_modmask;
    auto size_to_end = std::min(count, get_buffer_capacity() - index);
    size_t size_read = 0;

    if constexpr(g_has_min_contiguous_size)
    {
      if(size_to_end < g_min_contiguous_size)
      {
        size_read += has_min_contiguous_size_func_wrapper(func, index, size_to_end);
      }
      else
      {
        size_read += func(m_buffer->data() + index, size_to_end);
      }
    }
    else
    {
      size_read += func(m_buffer->data() + index, size_to_end);
    }

    m_index += size_read;

    if((size_read == size_to_end) && (size_read < count))
    {
      index              = m_index & g_modmask;
      size_t size_read_2 = 0;

      if constexpr(g_has_min_contiguous_size)
      {
        if(count - size_read < g_min_contiguous_size)
        {
          size_read_2 +=
            has_min_contiguous_size_func_wrapper(func, index, count - size_read);
        }
        else
        {
          size_read_2 += func(m_buffer->data() + index, count - size_read);
        }
      }
      else
      {
        size_read_2 += func(m_buffer->data() + index, count - size_read);
      }

      m_index += size_read_2;
      size_read += size_read_2;
    }

    return size_read;
  }

  std::vector<value_type> read(size_t count)
  {
    std::vector<value_type> values(count);

    read(values.data(), count);

    return values;
  }

  private:
  template <typename Func>
  size_t has_min_contiguous_size_func_wrapper(Func&& func, size_t index, size_t count)
  {
    assert(count < g_min_contiguous_size);

    std::memcpy(m_min_contiguous_buffer->data(),
      m_buffer->data() + index,
      count * sizeof(value_type));

    auto read_size =
      std::min(func(m_min_contiguous_buffer->data(), g_min_contiguous_size), count);

    return read_size;
  }
};

template <size_t MIN_CONTIGUOUS_SIZE>
constexpr auto make_reader(circular_buffer_ptr auto buffer)
{
  return reader<decltype(buffer), MIN_CONTIGUOUS_SIZE>(buffer);
}

} // namespace plz::circbuff

#endif // __CIRCBUFF_CIRCULAR_READER_H__