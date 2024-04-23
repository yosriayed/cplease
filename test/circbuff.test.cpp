#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

#include "plz/circbuff/reader.hpp"
#include "plz/circbuff/writer.hpp"

TEST_CASE("circbuff: is_buffer_pointer")
{
  static_assert(plz::is_power_of_2(10) == false);
  static_assert(plz::is_power_of_2(16) == true);

  static_assert(plz::circbuff::circular_buffer_ptr<std::array<int, 10>*> == false);
  static_assert(plz::circbuff::circular_buffer_ptr<std::array<int, 16>*> == true);
  static_assert(plz::circbuff::circular_buffer_ptr<std::array<int, 0>*> == true);
  static_assert(plz::circbuff::circular_buffer_ptr<std::array<int, 4>*> == true);
  static_assert(
    plz::circbuff::circular_buffer_ptr<std::array<int, size_t(1 << 14)>*> == true);
}

TEST_CASE("circbuff: reader constructor")
{
  SECTION("Raw ptr")
  {
    std::array<int, 16> array;
    plz::circbuff::reader reader(&array);

    REQUIRE(reader.get_index() == 0);
    REQUIRE(reader.get_buffer_capacity() == 16);
  }

  SECTION("unique_ptr")
  {
    auto array = std::make_unique<std::array<int, 16>>();
    plz::circbuff::reader reader(std::move(array));

    REQUIRE(reader.get_index() == 0);
    REQUIRE(reader.get_buffer_capacity() == 16);
  }

  SECTION("shared_ptr")
  {
    auto array = std::make_shared<std::array<int, 16>>();
    plz::circbuff::reader reader(array);

    REQUIRE(reader.get_index() == 0);
    REQUIRE(reader.get_buffer_capacity() == 16);
  }

  SECTION("optional")
  {
    std::optional array = std::array<int, 16>();
    plz::circbuff::reader reader(array);

    REQUIRE(reader.get_index() == 0);
    REQUIRE(reader.get_buffer_capacity() == 16);
  }
}

TEST_CASE("circbuff: writer constructor")
{
  SECTION("Raw ptr")
  {
    std::array<int, 16> array;
    plz::circbuff::writer reader(&array);

    REQUIRE(reader.get_index() == 0);
    REQUIRE(reader.get_buffer_capacity() == 16);
  }

  SECTION("unique_ptr")
  {
    auto array = std::make_unique<std::array<int, 16>>();
    plz::circbuff::writer writer(std::move(array));

    REQUIRE(writer.get_index() == 0);
    REQUIRE(writer.get_buffer_capacity() == 16);
  }

  SECTION("shared_ptr")
  {
    auto array = std::make_shared<std::array<int, 16>>();
    plz::circbuff::writer writer(array);

    REQUIRE(writer.get_index() == 0);
    REQUIRE(writer.get_buffer_capacity() == 16);
  }

  SECTION("optional")
  {
    std::optional array = std::array<int, 16>();
    plz::circbuff::writer writer(array);

    REQUIRE(writer.get_index() == 0);
    REQUIRE(writer.get_buffer_capacity() == 16);
  }
}

TEST_CASE("circbuff writer put and write functions")
{
  std::array<int, 16> array;
  plz::circbuff::writer writer(&array);
  plz::circbuff::reader reader(&array);

  SECTION("circbuff put function")
  {
    writer.put(1);
    writer.put(2);
    writer.put(3);

    REQUIRE(reader.get_span_0()[0] == 1);
    REQUIRE(reader.get_span_0()[1] == 2);
    REQUIRE(reader.get_span_0()[2] == 3);
  }

  SECTION("circbuff write function")
  {
    std::vector<int> values = { 4, 5, 6 };
    writer.write(values.data(), values.size());

    REQUIRE(reader.get_span_0()[0] == 4);
    REQUIRE(reader.get_span_0()[1] == 5);
    REQUIRE(reader.get_span_0()[2] == 6);
  }
}

TEST_CASE("circbuff writer write_using function")
{
  std::array<int, 16> array;
  plz::circbuff::writer writer(&array);
  plz::circbuff::reader reader(&array);

  SECTION("write_using function")
  {
    size_t data_written = writer.write_using(
      [](int* data, size_t count)
      {
        for(size_t i = 0; i < count; i++)
        {
          data[i] = i + 1;
        }
        return count;
      },
      3);

    REQUIRE(data_written == 3);
    REQUIRE(reader.get_span_0()[0] == 1);
    REQUIRE(reader.get_span_0()[1] == 2);
    REQUIRE(reader.get_span_0()[2] == 3);
  }

  SECTION("read function")
  {
    writer.put(4);
    writer.put(5);
    writer.put(6);

    std::vector<int> values(3);
    reader.read(values.data(), values.size());

    REQUIRE(values[0] == 4);
    REQUIRE(values[1] == 5);
    REQUIRE(values[2] == 6);
  }
}

TEST_CASE("circbuff reader read_using function")
{
  std::array<int, 16> array;
  plz::circbuff::writer writer(&array);
  plz::circbuff::reader reader(&array);

  SECTION("read_using function")
  {
    writer.put(1);
    writer.put(2);
    writer.put(3);

    std::vector<int> values(3);
    size_t data_read = reader.read_using(
      [&values](int* data, size_t count)
      {
        for(size_t i = 0; i < count; i++)
        {
          values[i] = data[i];
        }
        return count;
      },
      3);

    REQUIRE(data_read == 3);
    REQUIRE(values[0] == 1);
    REQUIRE(values[1] == 2);
    REQUIRE(values[2] == 3);
  }
}

TEST_CASE("circbuff: overflow test")
{
  std::array<char, 8> array;
  plz::circbuff::writer writer(&array);
  plz::circbuff::reader reader(&array);

  writer.put('0');
  writer.put('1');
  writer.put('2');
  writer.put('3');

  auto read_data = reader.read(4);
  REQUIRE(std::memcmp(read_data.data(), array.data(), 4) == 0);
  REQUIRE(writer.get_index() == 4);
  REQUIRE(reader.get_index() == 4);

  writer.put('4');
  writer.put('5');
  writer.put('6');
  writer.put('7');

  REQUIRE(writer.get_index() == 8);
  REQUIRE(reader.get_index() == 4);

  read_data = reader.read(4);
  REQUIRE(std::memcmp(read_data.data(), array.data() + 4, 4) == 0);
  REQUIRE(writer.get_index() == 8);
  REQUIRE(reader.get_index() == 8);

  writer.put('8');

  REQUIRE(writer.get_index() == 9);
  REQUIRE(reader.get_index() == 8);

  auto p = reader.peek();
  REQUIRE(p == '8');
  REQUIRE(reader.get_index() == 8);
  p = reader.get();
  REQUIRE(p == '8');
  REQUIRE(reader.get_index() == 9);
  writer.put('9');

  REQUIRE(reader.get() == '9');
  REQUIRE(reader.get_index() == writer.get_index());

  writer.write("abcdefgh", 8);
  read_data = reader.read(8);
  REQUIRE(std::memcmp(read_data.data(), "abcdefgh", 8) == 0);
}

struct my_array
{
  int m_data[16];

  int* data()
  {
    return m_data;
  }
};

template <>
struct plz::array_traits<my_array>
{
  constexpr static size_t capacity = 16;
  using value_type                 = int;
};

TEST_CASE("circbuff: custom array")
{
  my_array array;

  static_assert(plz::array_traits<my_array>::capacity == 16);
  static_assert(std::is_same_v<plz::array_traits<my_array>::value_type, int>);
  static_assert(plz::circbuff::circular_buffer_ptr<decltype(&array)>);

  plz::circbuff::writer writer(&array);
  plz::circbuff::reader reader(&array);

  writer.put(1);
  writer.put(2);
  writer.put(3);

  REQUIRE(reader.get_span_0()[0] == 1);
  REQUIRE(reader.get_span_0()[1] == 2);
  REQUIRE(reader.get_span_0()[2] == 3);
}

TEST_CASE("circbuff: minimum contigious buffer write")
{
  constexpr size_t min_contiguous_size = 4;

  std::array<char, 8> array;
  auto writer = plz::circbuff::make_writer<4>(&array);

  // plz::circbuff::writer<std::array<char, 8>*, min_contiguous_size> writer(&array);
  plz::circbuff::reader reader(&array);
  const char* src_data = "123456789abcdef";

  size_t offset       = 0;
  auto write_func_min = [src_data, &offset](char* data, size_t count)
  {
    REQUIRE(count >= min_contiguous_size);
    std::memcpy(data, src_data + offset % strlen(src_data), count);
    offset += count;
    return count;
  };

  writer.write_using(write_func_min, 4);
  writer.write_using(write_func_min, 2);

  REQUIRE(reader.read(6) == std::vector<char>({ '1', '2', '3', '4', '5', '6' }));
}

TEST_CASE("circbuff: minimum contigious buffer read")
{
  constexpr size_t min_contiguous_size = 4;

  std::array<char, 16> array;
  auto writer          = plz::circbuff::make_writer(&array);
  auto reader          = plz::circbuff::make_reader<4>(&array);
  const char* src_data = "0123456789abcdef";

  writer.write(src_data, strlen(src_data));

  int index          = 0;
  auto read_min_func = [&index, src_data](char* data, size_t count)
  {
    REQUIRE(count >= min_contiguous_size);
    return count;
  };

  index += reader.read_using(read_min_func, 4);
  index += reader.read_using(read_min_func, 2);
  index += reader.read_using(read_min_func, 8);

  REQUIRE(index == 14);
  REQUIRE(reader.get_index() == index);
  writer.write("gh", 2);

  auto reader2 = reader;
  REQUIRE(reader2.get_index() == reader.get_index());
  REQUIRE(reader2.get_index() == 14);

  auto read = reader.read_using(read_min_func, 4);
  REQUIRE(read == 4);

  auto peeked = reader2.peek(4);
  REQUIRE(peeked == std::vector<char>({ 'e', 'f', 'g', 'h' }));
}