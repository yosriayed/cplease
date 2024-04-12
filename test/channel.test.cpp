#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "plz/circbuff/channel.hpp"

TEST_CASE("channel: make test")
{
  std::array<char, 1024> buffer;
  auto [src, sink] = plz::make_channel(&buffer);

  CHECK(src.get_buffer_capacity() == sink.get_buffer_capacity());

  CHECK(sink.get_available_data_size() == 0);

  src.write("Hello", 5);
  CHECK(sink.get_available_data_size() == 5);
  std::array<char, 11> data;

  CHECK(sink.read(data.data(), 5));

  CHECK(data[0] == 'H');
  CHECK(data[1] == 'e');
  CHECK(data[2] == 'l');
  CHECK(data[3] == 'l');
  CHECK(data[4] == 'o');

  src.write(" World", 6);
  CHECK(sink.get_available_data_size() == 6);

  CHECK(sink.read(data.data() + 5, 6));

  CHECK(std::memcmp(data.data(), "Hello World", 11) == 0);
  CHECK(sink.get_available_data_size() == 0);
}

TEST_CASE("channel: write and read test")
{
  std::array<int, 16> array;
  auto [src, sink] = plz::make_channel(&array);

  std::array<int, 5> data = { 1, 2, 3, 4, 5 };

  // Write data to the channel
  src.write(data.data(), data.size());

  // Read data from the channel
  std::array<int, 5> readData;
  sink.read(readData.data(), readData.size());

  // Check if the read data matches the written data
  CHECK(std::memcmp(data.data(), readData.data(), data.size() * sizeof(int)) == 0);
}

TEST_CASE("spmc_channel: multiple sinks test")
{
  std::array<int, 16> array;
  auto [src, sinks] = plz::make_spmc_channel<2>(&array);

  auto& sink1 = sinks[0];
  auto& sink2 = sinks[1];

  std::array<int, 5> data = { 1, 2, 3, 4, 5 };

  // Write data to the channel
  src.write(data.data(), data.size());

  // Read data from the first sink
  std::array<int, 5> readData1;
  sink1.read(readData1.data(), readData1.size());

  // Read data from the second sink
  std::array<int, 5> readData2;
  sink2.read(readData2.data(), readData2.size());

  // Check if the read data matches the written data for both sinks
  CHECK(std::memcmp(data.data(), readData1.data(), data.size() * sizeof(int)) == 0);
  CHECK(std::memcmp(data.data(), readData2.data(), data.size() * sizeof(int)) == 0);
}

TEST_CASE("channel: overflow test")
{
  std::array<int, 8> array;
  auto [src, sink] = plz::make_channel(&array);

  auto second_sink = sink.clone();
  int i            = 0;
  for(; i < src.get_buffer_capacity(); i++)
  {
    src.put(i);
    CHECK(sink.get() == i);
  }

  CHECK(src.get_buffer_capacity() == i);
  CHECK(sink.get_available_data_size() == 0);
  CHECK(second_sink.get_available_data_size() == src.get_buffer_capacity());

  src.put(i);

  CHECK(sink.get() == i);

  for(int j = 1; j < src.get_buffer_capacity(); j++)
  {
    CHECK(sink.get() == j);
  }
}

TEST_CASE("channel: read using")
{
  std::array<int, 1024> array;
  auto [src, sink] = plz::make_channel(&array);

  std::jthread writer(
    [src = std::move(src)](std::stop_token token) mutable
    {
      int i = 0;

      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        src.put(i++);
      }
    });

  std::jthread reader(
    [sink = std::move(sink)](std::stop_token token) mutable
    {
      int i = 0;

      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(sink.get_available_data_size() > 0)
        {
          sink.read_using(
            [&i](auto data, auto size)
            {
              for(size_t j = 0; j < size; j++)
              {
                CHECK(data[j] == i++);
              }
              return size;
            },
            sink.get_available_data_size());
        }
      }
    });

  std::this_thread::sleep_for(std::chrono::seconds(3));
  writer.request_stop();
  reader.request_stop();
}

TEST_CASE("channel: notify function")
{
  std::array<char, 1024> array;
  auto [src, sink] = plz::make_channel(&array);

  src.register_notify_function(
    [sink = std::move(sink)](size_t count) mutable
    {
      plz::async::run(
        [&sink](size_t count)
        {
          sink.read_using(
            [](auto data, auto size)
            {
              CHECK(size == 5);
              CHECK(std::string_view(data, size) == "Hello");
              return size;
            },
            count);
        },
        count);
    });

  plz::async::run(
    [&src]
    {
      for(int i = 0; i < 10; i++)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        src.write("Hello", 5);
      }
    });

  plz::async::wait();
}

TEST_CASE("channel: connect")
{
  std::array<char, 16> array;
  auto [src, sink] = plz::make_channel(&array);

  plz::connect(&src,
    &sink,
    [](auto data, auto size)
    {
      return size;
    });

  plz::connect(
    &src,
    &sink,
    [](auto data, auto size)
    {
      return size;
    },
    &plz::async::thread_pool::global_instance());

  plz::async::run(
    [&src]
    {
      for(int i = 0; i < 10; i++)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        src.write("Hello", 5);
      }
    });

  plz::async::wait();
}

TEST_CASE("spmc_channel: connect data integrality")
{
  constexpr int num_sinks = 3;
  std::array<char, 1024> array;

  auto [src, sinks] = plz::make_spmc_channel<num_sinks>(&array);

  const auto data =
    "0-1-2-3-4-5-6-7-8-9-10-11-12-13-14-15-16-17-18-19-20-21-22-23-24-25-26-27-"
    "28-29-30-31-32-33-34-35-36-37-38-39-40-41-42-43-44-45-46-47-48-49-50-51-"
    "52-53-54-55-56-57-58-59-60-61-62-63-64-65-66-67-68-69-70-71-72-73-74-75-"
    "76-77-78-79-80-81-82-83-84-85-86-87-88-89-90-91-92-93-94-95-96-97-98-99-"
    "100-101-102-103-104-105-106-107-108-109-110-111-112-113-114-115-116-117-"
    "118-119-120-121-122-123-124-125-126-127-128-129-130-131-132-133-134-135-"
    "136-137-138-139-140-141-142-143-144-145-146-147-148-149-150-151-152-153-"
    "154-155-156-157-158-159-160-161-162-163-164-165-166-167-168-169-170-171-"
    "172-173-174-175-176-177-178-179-180-181-182-183-184-185-186-187-188-189-"
    "190-191-192-193-194-195-196-197-198-199-200-201-202-203-204-205-206-207-"
    "208-209-210-211-212-213-214-215-216-217-218-219-220-221-222-223-224-225-"
    "226-227-228-229-230-231-232-233-234-235-236-237-238-239-240-241-242-243-"
    "244-245-246-247-248-249-250";

  std::array<std::string, num_sinks> m_sink_data;
  for(int i = 0; i < num_sinks; i++)
  {
    plz::async_connect(&src,
      &sinks[i],
      [i, &m_sink_data](auto data, auto size)
      {
        m_sink_data[i].append(std::string_view(data, size));
        return size;
      });
  }

  plz::async::run(
    [&src, data = std::string_view(data)]
    {
      constexpr int num_writes = 10;
      size_t num_bytes         = data.size() / num_writes;
      size_t index             = 0;
      for(int i = 0; i <= num_writes; i++)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto num_bytes = std::min(data.size() / num_writes, data.size() - index);
        src.write(data.data() + index, num_bytes);
        index += num_bytes;
      }
    })
    .wait();

  for(int i = 0; i < num_sinks; i++)
  {
    CHECK(m_sink_data[i] == data);
  }
}

TEST_CASE("channel: disconnect source should return true")
{
  std::array<int, 16> array;
  auto [src, sink] = plz::make_channel(&array);

  auto connection = plz::source_connection{ src.register_notify_function(
    [](int value)
    {
    }) };

  // Act
  bool result = disconnect(&src, connection);

  // Assert
  REQUIRE(result == true);
}

TEST_CASE("channel: Disconnect source connection should no longer call "
          "the sink's handler")
{
  std::array<char, 16> array;

  auto [src, sink] = plz::make_channel(&array);
  size_t size_read = 0;
  auto connection  = plz::connect(&src,
    &sink,
    [&size_read](auto data, auto size)
    {
      size_read += size;
      return size;
    });

  src.write("1234567", 7);
  CHECK(size_read == 7);

  CHECK(disconnect(&src, connection));

  src.write("1234567", 7);

  // The size_read should not change
  CHECK(size_read == 7);
}

TEST_CASE("channel: multiple sources")
{
  std::array<char, 16> array;

  auto [srcs, sink] = plz::make_mpsc_channel<2>(&array);

  auto& src0 = srcs[0];
  auto& src1 = srcs[1];
  auto src2  = src0.clone();

  src0.put('0');
  src1.put('1');
  src2.put('2');

  CHECK(sink.read(3) == std::vector{ '0', '1', '2' });
}