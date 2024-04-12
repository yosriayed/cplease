#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <expected>
#include <numeric>
#include <random>
#include <string>

#include <plz/task.hpp>
#include <plz/thread_pool.hpp>

using namespace std::literals::chrono_literals;

static int foo(int i, int x)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  return i + x;
}

TEST_CASE("async_tasks: run async global instance")
{
  REQUIRE(plz::async::run(foo, 1, 2).wait() == 3);
  plz::async::wait();
}

TEST_CASE("async_tasks: enqueue and execute a task")
{
  plz::async::thread_pool pool(1);

  int result = 0;

  auto task = [&result]()
  {
    result = 42;
  };

  pool.run(task);

  // Sleep for a while to let the thread execute the task
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(result == 42);
}

TEST_CASE("async_tasks: enqueue and execute multiple tasks")
{
  plz::async::thread_pool pool(3);

  int result = 0;

  auto task = [&result]()
  {
    result += 10;
  };

  for(int i = 0; i < 5; ++i)
  {
    pool.run(task);
  }

  // Sleep for a while to let the threads execute the tasks
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(result == 50);
}

TEST_CASE("async_tasks: enqueue and execute tasks with futures")
{
  plz::async::thread_pool pool(1);

  auto task = [](int x)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return x;
  };

  pool.run(task, 84);
  auto future = pool.run(task, 42);

  // Retrieve the result from the future
  int result = future.wait();

  REQUIRE(result == 42);
}

TEST_CASE("async_tasks: enqueue on stopped ThreadPool")
{
  plz::async::thread_pool pool(1);

  pool.quit(); // Stop the ThreadPool

  REQUIRE_THROWS_AS(pool
                      .run(
                        []
                        {
                        })
                      .wait(),
    std::runtime_error);
}

TEST_CASE("async_tasks: enqueue on full ThreadPool")
{
  plz::async::thread_pool pool(1);

  auto task = [](int x)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return x;
  };

  pool.run(task, 84);
  auto future = pool.run(task, 42);

  // Retrieve the result from the future
  int result = future.wait();

  REQUIRE(result == 42);
}

TEST_CASE("async_tasks: chain using future::then")
{
  plz::async::thread_pool pool(1);

  auto v = pool
             .run(
               []
               {
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 return 42;
               })
             .then(
               [](int x)
               {
                 return x + 1;
               })
             .then(
               [](int x)
               {
                 return x - 1;
               })
             .wait();

  CHECK(v == 42);
}

TEST_CASE("async_tasks: exception")
{
  plz::async::thread_pool pool(1);

  REQUIRE_THROWS_AS(pool
                      .run(
                        []
                        {
                          std::this_thread::sleep_for(std::chrono::milliseconds(100));
                          return 42;
                        })
                      .then(
                        [](int x)
                        {
                          throw int(0);
                          return x + 1;
                        })
                      .then(
                        [](int x)
                        {
                          return x - 1;
                        })
                      .wait(),
    int);
}

TEST_CASE("async_tasks: cancellable task")
{
  plz::async::thread_pool pool(2);
  std::stop_source src;

  auto v = pool.run(
    [](std::stop_token token)
    {
      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    },
    src.get_token());

  pool.run(
    [&src]()
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      src.request_stop();
    });

  pool.wait();
}

TEST_CASE("async_tasks: wait")
{
  const int numTasks = 100; // Adjust the number of tasks as needed

  plz::async::thread_pool pool(16);

  // Submit tasks to the threadpool
  for(int i = 0; i < numTasks; ++i)
  {
    pool.run(
      []
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      });
  }

  // Wait for all tasks to complete
  pool.wait();
}

TEST_CASE("async_tasks: map")
{
  plz::async::thread_pool pool(4);

  std::array vec = { 1, 2, 3, 4, 5, 6, 7, 8 };
  auto f         = pool.map(vec,
    [](auto i)
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

TEST_CASE("async_tasks: map on string chars")
{
  plz::async::thread_pool pool(4);

  std::string name = "yosri";

  try
  {
    auto uppername = pool
                       .map(name,
                         [](auto i) -> char
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

    CHECK(uppername == "YOSRI");
  }
  catch(std::exception& e)
  {
  }
}

TEST_CASE("async_tasks: map individual handling of results")
{
  plz::async::thread_pool pool(4);

  std::string name = "yosri";

  auto futures = pool.map(name,
    [](auto i)
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

  CHECK(std::string(futures.wait().data(), 5) == "YOSRI");
}

TEST_CASE("async_tasks: async_map")
{
  std::string name = "yosri";

  auto r = plz::async::map(name,
    [](auto i) -> char
    {
      return std::toupper(i);
    });

  CHECK(std::memcmp(r.wait().data(), "YOSRI", 5) == 0);
}

TEST_CASE("async_tasks: map throw exception")
{
  plz::async::thread_pool pool(4);

  std::string name = "yosri";

  auto futures = pool.map(name,
    [](auto i)
    {
      if(i == 'o')
      {
        throw std::runtime_error("error");
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

  futures.get_future('o').on_exception(
    [](const std::runtime_error& exception)
    {
      CHECK(std::string(exception.what()) == "error");
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

  CHECK_THROWS_AS(std::string(futures.wait().data(), 5) == "YOSRI", std::runtime_error);
}

static int randomDelayFunction(int i)
{
  static std::random_device rd;
  static std::mt19937 gen(rd());

  static std::uniform_int_distribution<> delayDistribution(100, 1000);
  int delayMilliseconds = delayDistribution(gen);

  std::this_thread::sleep_for(std::chrono::milliseconds(delayMilliseconds));

  return 0;
}

TEST_CASE("async_tasks: map tasks with random delays")
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

TEST_CASE("async_tasks: chain tasks asyncronously")
{
  plz::async::thread_pool pool(1);

  auto v = pool
             .run(
               []
               {
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 return 42;
               })
             .then(&pool,
               [](int x)
               {
                 return x + 1;
               })
             .then(&pool,
               [](int x)
               {
                 return x - 1;
               })
             .wait();

  CHECK(v == 42);
}

TEST_CASE("async_tasks: map with Chain tasks asyncronously", )
{
  constexpr int size = 100;
  std::vector<int> v{};
  v.resize(size);
  std::iota(v.begin(), v.end(), 0);

  auto f = plz::async::map(v, randomDelayFunction)
             .async_then(
               [](const auto& r)
               {
                 auto ac = std::accumulate(r.begin(), r.end(), 0);
                 return ac;
               })
             .wait();

  CHECK(f == 0);
}

TEST_CASE("async_tasks: close tasks with jthread's stop token")
{
  plz::async::thread_pool pool(3);

  pool.run(
    [](int i, std::stop_token token)
    {
      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(100ms);
      }
    },
    42);

  pool.run(
    [](std::stop_token token)
    {
      while(!token.stop_requested())
      {
        std::this_thread::sleep_for(100ms);
      }
    });

  std::this_thread::sleep_for(2s);

  pool.quit();
  pool.wait();
}
