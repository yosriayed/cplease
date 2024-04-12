#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <expected>
#include <numeric>
#include <random>
#include <string>

#include <plz/expected/task.hpp>
#include <plz/thread_pool.hpp>

using namespace std::literals::chrono_literals;

enum class error_code
{
  error1,
  error2,
  error3
};

static auto foo(int i, int x) -> std::expected<int, error_code>
{
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  return i + x;
}

TEST_CASE("expected_async_tasks: run async global instance")
{
  REQUIRE(plz::async::run(foo, 1, 2).wait().value() == 3);
  plz::async::wait();
}

TEST_CASE("expected_async_tasks: enqueue and execute a task")
{
  plz::async::thread_pool pool(1);

  int result = 0;

  auto task = [&result]() -> std::expected<void, error_code>
  {
    result = 42;
    return {};
  };

  pool.run(task);

  // Sleep for a while to let the thread execute the task
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(result == 42);
}

TEST_CASE("expected_async_tasks: enqueue and execute multiple tasks")
{
  plz::async::thread_pool pool(3);

  int result = 0;

  auto task = [&result]() -> std::expected<void, error_code>
  {
    result += 10;
    return {};
  };

  for(int i = 0; i < 5; ++i)
  {
    pool.run(task);
  }

  // Sleep for a while to let the threads execute the tasks
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(result == 50);
}

TEST_CASE("expected_async_tasks: enqueue and execute tasks with futures")
{
  plz::async::thread_pool pool(1);

  auto task = [](int x) -> std::expected<int, error_code>
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return x;
  };

  pool.run(task, 84);
  auto future = pool.run(task, 42);

  // Retrieve the result from the future
  int result = future.wait().value();

  REQUIRE(result == 42);
}

TEST_CASE("expected_async_tasks: enqueue on stopped ThreadPool")
{
  plz::async::thread_pool pool(1);

  pool.quit(); // Stop the ThreadPool

  REQUIRE_THROWS_AS(pool
                      .run(
                        []() -> std::expected<void, error_code>
                        {
                          return {};
                        })
                      .wait(),
    std::runtime_error);
}

TEST_CASE("expected_async_tasks: enqueue on full ThreadPool")
{
  plz::async::thread_pool pool(1);

  auto task = [](int x) -> std::expected<int, error_code>
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return x;
  };

  pool.run(task, 84);
  auto future = pool.run(task, 42);

  // Retrieve the result from the future
  int result = future.wait().value();

  REQUIRE(result == 42);
}

TEST_CASE("expected_async_tasks: chain using future::then")
{
  plz::async::thread_pool pool(1);

  auto v = pool
             .run(
               []() -> std::expected<int, error_code>
               {
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 return 42;
               })
             .then(
               [](int x) -> std::expected<int, error_code>
               {
                 return x + 1;
               })
             .then(
               [](int x) -> std::expected<int, error_code>
               {
                 return x - 1;
               })
             .wait()
             .value();

  CHECK(v == 42);
}

TEST_CASE("expected_async_tasks: exception")
{
  plz::async::thread_pool pool(1);
  auto expected = pool
                    .run(
                      []() -> std::expected<int, error_code>
                      {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        return 42;
                      })
                    .then(
                      [](int x) -> std::expected<int, error_code>
                      {
                        return std::unexpected(error_code::error1);
                      })
                    .then(
                      [](int x) -> std::expected<int, error_code>
                      {
                        return x - 1;
                      })
                    .wait();
  REQUIRE(expected.has_value() == false);
  REQUIRE(expected.error() == error_code::error1);
}

TEST_CASE("expected_async_tasks: cancellable task")
{
  plz::async::thread_pool pool(2);
  std::stop_source src;

  auto v = pool.run(
    [](std::stop_token token) -> std::expected<void, error_code>
    {
      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      return {};
    },
    src.get_token());

  pool.run(
    [&src]() -> std::expected<void, error_code>
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      src.request_stop();
      return {};
    });

  pool.wait();
}

TEST_CASE("expected_async_tasks: wait")
{
  const int numTasks = 100; // Adjust the number of tasks as needed

  plz::async::thread_pool pool(16);

  // Submit tasks to the threadpool
  for(int i = 0; i < numTasks; ++i)
  {
    pool.run(
      []() -> std::expected<void, error_code>
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return {};
      });
  }

  // Wait for all tasks to complete
  pool.wait();
}

TEST_CASE("expected_async_tasks: map")
{
  plz::async::thread_pool pool(4);

  std::array vec = { 1, 2, 3, 4, 5, 6, 7, 8 };
  auto f         = pool.map(vec,
    [](auto i) -> std::expected<int, error_code>
    {
      return i + 1;
    });

  auto sum = f.then(
                [](const auto& res)
                {
                  int acc = 0;
                  for(auto r : res)
                  {
                    acc += r;
                  }
                  return acc;
                })
               .wait();

  CHECK(sum == 44);
}

TEST_CASE("expected_async_tasks: map on string chars")
{
  plz::async::thread_pool pool(4);

  std::string name = "yosri";

  auto uppername = pool
                     .map(name,
                       [](auto i) -> std::expected<char, error_code>
                       {
                         return std::toupper(i);
                       })
                     .then(
                       [](const auto& res)
                       {
                         std::string r;

                         for(auto c : res)
                         {
                           r += c;
                         }

                         return r;
                       })
                     .wait();

  CHECK(uppername.value() == "YOSRI");
}

TEST_CASE("expected_async_tasks: map individual handling of results")
{
  plz::async::thread_pool pool(4);

  std::string name = "yosri";

  auto futures = pool.map(name,
    [](auto i) -> std::expected<char, error_code>
    {
      return char(std::toupper(i));
    });

  futures.get_future('y').then(
    [](auto c)
    {
      CHECK(c == 'Y');
    });

  futures.get_future('o').then(
    [](auto c)
    {
      CHECK(c == 'O');
    });

  futures.get_future('s').then(
    [](auto c)
    {
      CHECK(c == 'S');
    });

  futures.get_future('r').then(
    [](auto c)
    {
      CHECK(c == 'R');
    });

  futures.get_future('i').then(
    [](auto c)
    {
      CHECK(c == 'I');
    });

  CHECK(std::string(futures.wait().value().data(), 5) == "YOSRI");
}

TEST_CASE("expected_async_tasks: async_map", "[ThreadPool]")
{
  std::string name = "yosri";

  auto r = plz::async::map(name,
    [](auto i) -> std::expected<char, error_code>
    {
      return std::toupper(i);
    })
             .then(
               [](const auto& res)
               {
                 CHECK(std::memcmp(res.data(), "YOSRI", 5) == 0);
               });

  plz::async::wait();

  CHECK(std::memcmp(r.wait().value().data(), "YOSRI", 5) == 0);
}

TEST_CASE("expected_async_tasks: map throw exception")
{
  plz::async::thread_pool pool(4);

  std::string name = "yosri";

  auto futures = pool.map(name,
    [](auto i) -> std::expected<char, error_code>
    {
      if(i == 'o')
      {
        return std::unexpected(error_code::error2);
      }
      return char(std::toupper(i));
    });

  futures.get_future('y').then(
    [](auto c)
    {
      CHECK(c == 'Y');
    });

  futures.get_future('o').then(
    [](auto c)
    {
      CHECK(c == 'O');
    });

  futures.get_future('o').on_error(
    [](const error_code& error)
    {
      CHECK(error == error_code::error2);
    });

  futures.get_future('s').then(
    [](auto c)
    {
      CHECK(c == 'S');
    });

  futures.get_future('i').then(
    [](auto c)
    {
      CHECK(c == 'I');
    });

  CHECK(futures.wait().error() == error_code::error2);
}

static auto randomDelayFunction(int i) -> std::expected<int, error_code>
{
  static std::random_device rd;
  static std::mt19937 gen(rd());

  static std::uniform_int_distribution<> delayDistribution(100, 1000);
  int delayMilliseconds = delayDistribution(gen);

  std::this_thread::sleep_for(std::chrono::milliseconds(delayMilliseconds));

  return 0;
}

TEST_CASE("expected_async_tasks: map tasks with random delays")
{
  constexpr int size = 100;
  std::vector<int> v{};
  v.resize(size);
  std::iota(v.begin(), v.end(), 0);

  auto f = plz::async::map(v, randomDelayFunction)
             .then(
               [](const auto& r)
               {
                 auto ac = std::accumulate(r.begin(), r.end(), 0);
                 return ac;
               })
             .wait();

  CHECK(f == 0);
}

TEST_CASE("expected_async_tasks: chain tasks asyncronously")
{
  plz::async::thread_pool pool(1);

  auto v = pool
             .run(
               []() -> std::expected<int, error_code>
               {
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 return 42;
               })
             .then(&pool,
               [](int x) -> std::expected<int, error_code>
               {
                 return x + 1;
               })
             .then(&pool,
               [](int x) -> std::expected<int, error_code>
               {
                 return x - 1;
               })
             .wait();

  CHECK(v == 42);
}

TEST_CASE("expected_async_tasks: map with Chain tasks asyncronously", )
{
  constexpr int size = 100;
  std::vector<int> v{};
  v.resize(size);
  std::iota(v.begin(), v.end(), 0);

  auto f = plz::async::map(v, randomDelayFunction)
             .async_then(
               [](const auto& r) -> std::expected<int, error_code>
               {
                 auto ac = std::accumulate(r.begin(), r.end(), 0);
                 return ac;
               })
             .wait();

  CHECK(f.value() == 0);
}

TEST_CASE("expected_async_tasks: close tasks with jthread's stop token")
{
  plz::async::thread_pool pool(3);

  pool.run(
    [](int i, std::stop_token token) -> std::expected<void, error_code>
    {
      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(100ms);
      }

      return {};
    },
    42);

  pool.run(
    [](std::stop_token token) -> std::expected<void, error_code>
    {
      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(100ms);
      }
      return {};
    });

  std::this_thread::sleep_for(2s);

  pool.quit();
  pool.wait();
}

TEST_CASE("expected_async_tasks: expected run")
{
  plz::async::thread_pool pool(1);

  auto task = [](int x)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return std::expected<int, char>(42);
  };

  auto future = pool.run(task, 84);

  // Retrieve the result from the future
  auto result = future.wait();

  REQUIRE(result.value() == 42);

  auto future2 = pool.run(task, 42)
                   .then(
                     [](int x) -> std::expected<int, char>
                     {
                       return std::unexpected('e');
                     })
                   .then(
                     [](int x) -> std::expected<std::string, char>
                     {
                       return std::to_string(x);
                     });

  auto result2 = future2.wait();

  REQUIRE(result2.error() == 'e');
}

TEST_CASE("expected_async_tasks: expected chain tasks asyncronously")
{
  plz::async::thread_pool pool(1);

  auto v = pool
             .run(
               []() -> std::expected<int, char>
               {
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 return 42;
               })
             .async_then(
               [](int x) -> std::expected<std::string, char>
               {
                 return std::to_string(x);
               })
             .async_then(
               [](std::string x) -> std::expected<int, char>
               {
                 return std::stoi(x);
               })
             .on_error(
               [](char e) -> std::expected<int, char>
               {
                 return 0;
               })
             .wait();

  CHECK(v.value() == 42);
}

