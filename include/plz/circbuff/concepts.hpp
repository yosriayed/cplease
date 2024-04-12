#ifndef __CIRCBUFF_BUFFER_H__
#define __CIRCBUFF_BUFFER_H__

#include <memory>

#include "plz/help/array_traits.hpp"
#include "plz/help/math.hpp"

namespace plz::circbuff
{

template <typename BufferPtr>
concept circular_buffer_ptr = requires(BufferPtr buffer_ptr) {
  typename std::pointer_traits<BufferPtr>::element_type;
  {
    buffer_ptr->data()
  } -> std::same_as<typename array_traits<typename std::pointer_traits<BufferPtr>::element_type>::value_type*>;
} && is_power_of_2(array_traits<typename std::pointer_traits<BufferPtr>::element_type>::capacity) == true;

} // namespace plz::circbuff

#endif // __BUFFER_H__