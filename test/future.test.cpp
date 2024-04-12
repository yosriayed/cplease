#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>

#include <plz/future.hpp>

TEST_CASE("future: same return type + syncrhonous tasks")
{
  auto promise = plz::async::make_promise<int>();

  auto future = promise.get_future()
                  .then(
                    [](int value)
                    {
                      return value + 1;
                    })
                  .then(
                    [](int value)
                    {
                      return value - 1;
                    });

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  CHECK(future.wait() == 42);
}

TEST_CASE("future: different return types + syncrhonous tasks", )
{
  auto promise = plz::async::make_promise<int>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  auto value = future
                 .then(
                   [](int value)
                   {
                     return std::to_string(value + 1);
                   })
                 .then(
                   [](const std::string& value)
                   {
                     return std::stoi(value) - 1;
                   })
                 .wait(); // Wait for the entire chain to complete

  CHECK(value == 42);
}

TEST_CASE("future: syncrhonous tasks with exception 1")
{
  auto promise = plz::async::make_promise<int>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  try
  {
    future
      .then(
        [](int value)
        {
          return value + 1;
        })
      .then(
        [](int value)
        {
          throw 0;
          return value;
        })
      .then(
        [](int)
        {
          REQUIRE(false); // shouldn't reach this code
        })
      .wait(); // Wait for the entire chain to complete
  }
  catch(const int& e)
  {
    promise.get_future();
    CHECK(e == 0);
  }
}

TEST_CASE("future: syncrhonous tasks with exception 2")
{
  auto promise = plz::async::make_promise<int>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  try
  {
    future
      .then(
        [](int value)
        {
          return value + 1;
        })
      .then(
        [](int value)
        {
          throw std::runtime_error("error");
          return value;
        })
      .then(
        [](int value)
        {
          throw 0;
          return value;
        })
      .then(
        [](int)
        {
          REQUIRE(false); // shouldn't reach this code
        })
      .wait(); // Wait for the entire chain to complete
  }
  catch(int)
  {
    REQUIRE(true); // shouldn't reach this code
  }
  catch(const std::exception& e)
  {
    CHECK(e.what() == std::string("error"));
  }
}

TEST_CASE("future: asyncrhonous tasks")
{
  auto promise = plz::async::make_promise<int>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  auto value = future
                 .then(
                   [](int value)
                   {
                     return value + 1;
                   })
                 .then(
                   [](int value)
                   {
                     auto promise = plz::async::make_promise<std::string>();
                     auto future  = promise.get_future();

                     // Simulating an asynchronous task
                     std::thread(
                       [promise, value]() mutable
                       {
                         std::this_thread::sleep_for(std::chrono::milliseconds(100));
                         promise.set_result(std::to_string(value - 1));
                       })
                       .detach();

                     return future;
                   })
                 .then(
                   [](const std::string& value)
                   {
                     return std::stoi(value);
                   })
                 .wait(); // Wait for the entire chain to complete

  CHECK(value == 42);
}

TEST_CASE("future: asyncrhonous tasks with exception")
{
  auto promise = plz::async::make_promise<int>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  try
  {
    future
      .then(
        [](int value)
        {
          return value + 1;
        })
      .then(
        [](int value)
        {
          auto promise = plz::async::make_promise<std::string>();
          auto future  = promise.get_future();

          // Simulating an asynchronous task
          std::thread(
            [promise, value]() mutable
            {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
              try
              {
                throw std::logic_error("logic_error");
              }
              catch(...)
              {
                promise.set_exception(std::current_exception());
              }
            })
            .detach();

          return future;
        })
      .then(
        [](const std::string& value)
        {
          return std::stoi(value);
        })
      .wait(); // Wait for the entire chain to complete
  }
  catch(const std::exception& e)
  {
    CHECK(e.what() == std::string("logic_error"));
  }
}

auto asynchTask1(int value)
{
  auto promise = plz::async::make_promise<int>();

  // Simulating an asynchronous task
  std::thread(
    [promise, value]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(value);
    })
    .detach();

  return promise.get_future();
};

auto asynchTask2(int value)
{
  auto promise = plz::async::make_promise<std::string>();

  // Simulating an asynchronous task
  std::thread(
    [promise, value]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(std::to_string(value + 1));
    })
    .detach();

  return promise.get_future();
};

auto asynchTask3(const std::string& value)
{
  auto promise = plz::async::make_promise<int>();

  // Simulating an asynchronous task
  std::thread(
    [promise, value]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(std::stoi(value) - 1);
    })
    .detach();

  return promise.get_future();
};

TEST_CASE("future: asyncrhonous tasks functions")
{
  try
  {
    auto value = asynchTask1(42).then(asynchTask2).then(asynchTask3).wait();
    CHECK(value == 42);
  }
  catch(...)
  {
    REQUIRE(false);
  }
}

auto asynchTask2_5(std::string value)
{
  auto promise = plz::async::make_promise<std::string>();

  // Simulating an asynchronous task
  std::thread(
    [promise, value]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try
      {
        throw std::runtime_error("error");
        promise.set_result(value);
      }
      catch(...)
      {
        promise.set_exception(std::current_exception());
      }
    })
    .detach();

  return promise.get_future();
};

TEST_CASE("future: asyncrhonous Tasks Functions With Exception")
{
  try
  {
    asynchTask1(42).then(asynchTask2).then(asynchTask2_5).then(asynchTask3).wait();
  }
  catch(const std::exception& exception)
  {
    CHECK(std::string("error") == exception.what());
  }
}

TEST_CASE("future: oexpectedion")
{
  auto promise = plz::async::make_promise<int>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  try
  {
    future
      .then(
        [](int value)
        {
          return value + 1;
        })
      .then(
        [](int value)
        {
          throw std::runtime_error("error");
          return value;
        })
      .then(
        [](int)
        {
          REQUIRE(false); // shouldn't reach this code
        })
      .on_exception(
        [](const int&)
        {
          REQUIRE(false); // shouldn't reach this code
        })
      .on_exception(
        [](const std::logic_error&)
        {
          REQUIRE(false); // shouldn't reach this code
        })
      .on_exception(
        [](const std::runtime_error& exception)
        {
          CHECK(std::string("error") == exception.what());
        })
      .on_exception(
        [](const std::exception&)
        {
          REQUIRE(false); // shouldn't reach this code
        })
      .wait(); // Wait for the entire chain to complete
  }
  catch(const int&)
  {
    REQUIRE(true); // shouldn't reach this code
  }
  catch(const std::exception& exception)
  {
    CHECK(std::string("error") == exception.what());
  }
}

class FutureMemberFunctions
{
  public:
  auto memberAsynchTask1(int value)
  {
    auto promise = plz::async::make_promise<int>();

    // Simulating an asynchronous task
    std::thread(
      [promise, value]() mutable
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        promise.set_result(value);
      })
      .detach();

    return promise.get_future();
  };

  auto memberAsynchTask2(int value)
  {
    auto promise = plz::async::make_promise<std::string>();

    // Simulating an asynchronous task
    std::thread(
      [promise, value]() mutable
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        promise.set_result(std::to_string(value + 1));
      })
      .detach();

    return promise.get_future();
  };

  auto memberAsynchTask2_5(std::string value)
  {
    auto promise = plz::async::make_promise<std::string>();

    throw std::runtime_error("error");

    // Simulating an asynchronous task
    std::thread(
      [promise, value]() mutable
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise.set_result(value);
      })
      .detach();

    return promise.get_future();
  };

  auto memberAsynchTask3(std::string value)
  {
    auto promise = plz::async::make_promise<int>();

    // Simulating an asynchronous task
    std::thread(
      [promise, value]() mutable
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        promise.set_result(std::stoi(value) - 1);
      })
      .detach();

    return promise.get_future();
  };
};

TEST_CASE("future: futureMemberFunctions + AsynchronousTasks")
{
  FutureMemberFunctions q;
  auto value = q.memberAsynchTask1(42)
                 .then(&q, &FutureMemberFunctions::memberAsynchTask2)
                 .then(&q, &FutureMemberFunctions::memberAsynchTask3)
                 .then(
                   [](int value)
                   {
                     return value;
                   })
                 .wait();

  CHECK(value == 42);
}

TEST_CASE("future: futureMemberFunctions + AsynchronousTasksWithException")
{
  try
  {
    FutureMemberFunctions q;
    q.memberAsynchTask1(42)
      .then(&q, &FutureMemberFunctions::memberAsynchTask2)
      .then(&q, &FutureMemberFunctions::memberAsynchTask2_5)
      .then(
        [](std::string value)
        {
          return std::stoi(value);
        })
      .wait();
  }
  catch(const std::exception& exception)
  {
    CHECK(std::string("error") == exception.what());
  }
}

TEST_CASE("future: test with non blocking wait")
{
  FutureMemberFunctions q;
  auto future = q.memberAsynchTask1(42)
                  .then(
                    [](int i)
                    {
                      return std::to_string(i);
                    })
                  .then(
                    [](std::string i)
                    {
                      return std::stoi(i);
                    })
                  .then(
                    [](int value)
                    {
                      return value;
                    });

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  CHECK(future.wait() == 42);
}

TEST_CASE("future:Then with variadic arguments")
{
  FutureMemberFunctions q;
  auto future = q.memberAsynchTask1(42)
                  .then(
                    [](int i, int j)
                    {
                      return i + j;
                    },
                    2)
                  .then(
                    [](int i)
                    {
                      return i;
                    })
                  .then(
                    [](int value, int i, int k)
                    {
                      return value - i - k;
                    },
                    1,
                    1);

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  CHECK(future.wait() == 42);
}

TEST_CASE("future: test with move only type")
{
  auto promise = plz::async::make_promise<std::unique_ptr<int>>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(std::make_unique<int>(42));
    })
    .detach();

  auto value = future.take(); // Wait for the entire chain to complete

  CHECK(*value == 42);

  promise.set_result(std::make_unique<int>(43));

  CHECK(*future.wait() == 43);
}

TEST_CASE("future: test then with move only type")
{
  auto promise = plz::async::make_promise<std::unique_ptr<int>>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(std::make_unique<int>(42));
    })
    .detach();

  auto value = future
                 .then(
                   [](auto&& res)
                   {
                     return std::make_unique<std::string>(std::to_string(*res));
                   })
                 .then(
                   [](const auto& res)
                   {
                     return std::make_unique<int>(std::stoi(*res));
                   })
                 .take();

  auto c = future.wait();

  CHECK(*value == 42);
}