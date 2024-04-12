#ifndef MISC_H
#define MISC_H

#include <cmath>
#include <numeric>

namespace plz
{

constexpr size_t round_up_power2(size_t num)
{
  if(num == 0)
    return 1;

  auto sizebits = sizeof(num);

  // Subtract 1 to set all the lower bits to 1
  num--;

  // Bitwise OR with the result of right-shifting to fill the lower bits with 1
  num |= num >> 1;
  num |= num >> 2;
  num |= num >> 4;
  num |= num >> 8;
  num |= num >> 16;

  if(sizebits > 32)
  {
    num |= num >> 32;
  }

  // Add 1 to get the next power of 2
  return num + 1;
}

constexpr bool is_power_of_2(size_t num)
{
  return (num & (num - 1)) == 0;
}

template <typename T>
T lcm(T first, T second)
{
  if(first == 0 || second == 0)
  {
    return 0;
  }

  return (first * second) / std::gcd(first, second);
}
} // namespace plz

#endif