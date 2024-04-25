#ifndef __CIRCBUFF_CONCEPTS_H__
#define __CIRCBUFF_CONCEPTS_H__

#include <concepts>
#include <memory>

#include "plz/help/array_traits.hpp"
#include "plz/help/math.hpp"

namespace plz::circbuff
{

// Checks if a type is a pointer to a type that behaves like an array with a compile time known capacity
template <typename ArrayPtr>
concept array_ptr = requires(ArrayPtr ptr) {
  // ArrayPtr should be a pointer type. This is checked using std::pointer_traits<ArrayPtr>::element_type,
  // which extracts the element type pointed to by ArrayPtr.
  typename std::pointer_traits<ArrayPtr>::element_type; // Check if ArrayPtr is a pointer type

  // The type pointed to by ArrayPtr should have a corresponding array_traits
  // specialization. This is checked by trying to access array_traits<typename
  // std::pointer_traits<ArrayPtr>::element_type>::value_type.
  typename array_traits<typename std::pointer_traits<ArrayPtr>::element_type>::value_type;

  // The array_traits specialization of the type pointed to by ArrayPtr should
  // have a capacity member that is integral type
  {
    array_traits<typename std::pointer_traits<ArrayPtr>::element_type>::capacity
  } -> std::convertible_to<size_t>;

  // The type pointed to by ArrayPtr should have a data() member function that
  // returns a pointer to its underlying data. The return type of this function should be the same as the value
  // type of the array_traits of the element type pointed to by ArrayPtr.
  {
    ptr->data()
  } -> std::same_as<typename array_traits<typename std::pointer_traits<ArrayPtr>::element_type>::value_type*>;
};

// Check if a type is a pointer to a type that behaves like a circular buffer.
// i.e. it satisfy array_ptr concept and the capacity of the array is a power of 2
template <typename BufferPtr>
concept circular_buffer_ptr = array_ptr<BufferPtr> &&
  is_power_of_2(array_traits<typename std::pointer_traits<BufferPtr>::element_type>::capacity);

} // namespace plz::circbuff

#endif // __CIRCBUFF_CONCEPTS_H__