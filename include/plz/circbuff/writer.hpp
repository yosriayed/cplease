#ifndef ____CIRCBUFF_CIRCULAR_WRITER_H__
#define ____CIRCBUFF_CIRCULAR_WRITER_H__

#include <cassert>
#include <cstring>
#include <type_traits>

#include "plz/help/type_traits.hpp"

#include "concepts.hpp"

namespace plz::circbuff
{

template <size_t MIN_CONTIGUOUS_SIZE = 0>
constexpr auto make_writer(circular_buffer_ptr auto buffer);

template <typename BufferPointer, size_t MIN_CONTIGUOUS_SIZE = 0>
  requires circular_buffer_ptr<BufferPointer>
class writer
{
  static constexpr bool g_has_min_contiguous_size = (MIN_CONTIGUOUS_SIZE > 0);
  static constexpr size_t g_min_contiguous_size   = MIN_CONTIGUOUS_SIZE;

  public:
  using buffer_pointer = BufferPointer;
  using array_type = typename std::pointer_traits<BufferPointer>::element_type;
  using value_type =
    typename array_traits<typename std::pointer_traits<BufferPointer>::element_type>::value_type;

  std::conditional_t<g_has_min_contiguous_size, std::unique_ptr<std::array<value_type, g_min_contiguous_size>>, void*> m_min_contiguous_buffer =
    nullptr;

  constexpr static size_t get_buffer_capacity()
  {
    return array_traits<array_type>::capacity;
  }

  private:
  constexpr static size_t g_modmask = get_buffer_capacity() - 1;

  buffer_pointer m_buffer;
  std::atomic<size_t> m_index{ 0 };

  public:
  writer(BufferPointer buffer) : m_buffer{ std::move(buffer) }
  {
    if constexpr(g_has_min_contiguous_size)
    {
      m_min_contiguous_buffer =
        std::make_unique<std::array<value_type, g_min_contiguous_size>>();
    }
  }

  public:
  size_t get_index() const
  {
    return m_index;
  }

  /**
   * Writes a value to the channel.
   *
   * @param value The value to write to the channel.
   */
  void put(const value_type& value)
    requires((!g_has_min_contiguous_size) || (g_min_contiguous_size == 1))
  {
    m_buffer->data()[m_index & g_modmask] = value;
    m_index++;
  }

  /**
   * Writes a value to the channel.
   *
   * @param value The value to write to the channel.
   */
  void put(value_type&& value)
    requires((!g_has_min_contiguous_size) || (g_min_contiguous_size == 1))
  {
    m_buffer->data()[m_index & g_modmask] = std::move(value);
    m_index++;
  }

  void write(const value_type* values, size_t count)
  {
    size_t offset = 0;
    write_using(
      [&offset, values](value_type* data, size_t size)
      {
        std::memcpy(data, values + offset, size * sizeof(value_type));
        offset += size;
        return size;
      },
      count);
  };

  template <typename Func>
    requires std::invocable<Func, value_type*, size_t> &&
    std::same_as<std::invoke_result_t<Func, value_type*, size_t>, size_t>
  size_t write_using(Func&& func, size_t count)
  {
    auto index          = m_index & g_modmask;
    auto size_to_end    = std::min(count, get_buffer_capacity() - index);
    size_t size_written = 0;

    if constexpr(g_has_min_contiguous_size)
    {
      if(size_to_end < g_min_contiguous_size)
      {
        size_written += has_min_contiguous_size_func_wrapper(func, index, size_to_end);
      }
      else
      {
        size_written += func(m_buffer->data() + index, size_to_end);
      }
    }
    else
    {
      size_written += func(m_buffer->data() + index, size_to_end);
    }

    m_index += size_written;

    if((size_written == size_to_end) && (size_written < count))
    {
      index = m_index & g_modmask;

      size_t size_written_2 = 0;

      if constexpr(g_has_min_contiguous_size)
      {
        if(count - size_written < g_min_contiguous_size)
        {
          size_written_2 =
            has_min_contiguous_size_func_wrapper(func, index, count - size_written);
        }
        else
        {
          size_written_2 = func(m_buffer->data() + index, count - size_written);
        }
      }
      else
      {
        size_written_2 = func(m_buffer->data() + index, count - size_written);
      }

      m_index += size_written_2;
      size_written += size_written_2;
    }

    return size_written;
  };

  private:
  template <typename Func>
  size_t has_min_contiguous_size_func_wrapper(Func&& func, size_t index, size_t count)
  {
    assert(count < g_min_contiguous_size);

    auto written =
      std::min(func(m_min_contiguous_buffer->data(), g_min_contiguous_size), count);

    std::memcpy(m_buffer->data() + index,
      m_min_contiguous_buffer->data(),
      written * sizeof(value_type));

    return written;
  }
};

template <size_t MIN_CONTIGIOUS_SIZE>
constexpr auto make_writer(circular_buffer_ptr auto buffer)
{
  return writer<decltype(buffer), MIN_CONTIGIOUS_SIZE>(buffer);
}

} // namespace plz::circbuff
#endif // ____CIRCBUFF_CIRCULAR_WRITER_H__