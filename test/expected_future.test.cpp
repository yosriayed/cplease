#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <thread>

#include <plz/future.hpp>

TEST_CASE("expected_future: simple set result")
{
  auto promise = plz::make_promise<int, std::string>();
  auto future  = promise.get_future();

  promise.set_result(42);

  REQUIRE(future.get() == 42);
}

TEST_CASE("expected_future: set result from another thread")
{
  auto promise = plz::make_promise<int, std::string>();
  auto future  = promise.get_future();

  std::jthread t(
    [&promise]
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    });

  REQUIRE(future.get() == 42);
}

TEST_CASE("expected_future: set error from another thread")
{
  auto promise = plz::make_promise<int, std::string>();
  auto future  = promise.get_future();

  std::jthread t(
    [&promise]
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      promise.set_result(std::unexpected("error"));
    });

  REQUIRE(future.get().error() == "error");
}

enum class Errcode
{
  error1,
  error2,
  error3
};

TEST_CASE("expected_future: set enum errorfrom another thread")
{
  auto promise = plz::make_promise<int, Errcode>();
  auto future  = promise.get_future();

  std::jthread t(
    [&promise]
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      promise.set_result(std::unexpected(Errcode::error2));
    });

  REQUIRE(future.get().error() == Errcode::error2);
}

TEST_CASE("expected_future: void result type")
{
  auto promise = plz::make_promise<void, std::string>();
  auto future  = promise.get_future();

  promise.set_ready();

  REQUIRE(future.get().has_value() == true);
}

TEST_CASE("expected_future: void result type set error")
{
  auto promise = plz::make_promise<void, std::string>();
  auto future  = promise.get_future();

  promise.set_error("error");

  REQUIRE(future.get().error() == "error");
}

TEST_CASE("expected_future: void result type then")
{
  auto promise = plz::make_promise<void, std::string>();

  auto future = promise.get_future().then(
    []()
    {
      return 42;
    });

  std::jthread t(
    [&promise]
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_ready();
    });

  REQUIRE(future.get() == 42);
}

TEST_CASE("expected_future: then return unexpected")
{
  auto promise = plz::make_promise<void, std::string>();

  auto future = promise.get_future().then(
    []() -> std::expected<int, std::string>
    {
      return std::unexpected("error");
    });

  std::jthread t(
    [&promise]
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_ready();
    });

  REQUIRE(future.get().has_value() == false);
  REQUIRE(future.get().error() == "error");
}

auto async_func()
{
  auto promise = plz::make_promise<int, std::string>();

  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);
    })
    .detach();

  return promise.get_future();
}

TEST_CASE("expected_future: async function")
{
  auto future = async_func().then(
    [](int t_value) -> std::expected<int, std::string>
    {
      return t_value;
    });

  REQUIRE(future.get() == 42);
}

enum class parse_error
{
  invalid_input,
  overflow
};

auto parse_number(std::string str)
{
  auto p      = plz::make_promise<double, parse_error>();
  auto future = p.get_future();
  std::thread{
    [p = std::move(p), str = std::move(str)]() mutable
    {
      const char* begin = str.data();
      char* end;
      double retval = std::strtod(begin, &end);

      if(begin == end)
      {
        p.set_result(std::unexpected(parse_error::invalid_input));
      }
      else if(std::isinf(retval))
      {
        p.set_result(std::unexpected(parse_error::overflow));
      }
      else
      {
        std::string_view(str).remove_prefix(end - begin);

        p.set_result(retval);
      }
    }
  }.detach();

  return future;
}

TEST_CASE("expected_future: cppreference std::expected test")
{
  REQUIRE(parse_number("42").get() == 42);
  REQUIRE(parse_number("42abc").get() == 42);
  REQUIRE(parse_number("meow").get().error() == parse_error::invalid_input);
  REQUIRE(parse_number("inf").get().error() == parse_error::overflow);
}

TEST_CASE("noexcept_future: test with move only type")
{
  auto promise = plz::make_promise<std::unique_ptr<int>, Errcode>();
  auto future  = promise.get_future();

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(std::make_unique<int>(42));
    })
    .detach();

  auto value = future.take().value(); // Wait for the entire chain to complete

  CHECK(*value == 42);

  // Simulating an asynchronous task
  std::thread(
    [promise]() mutable
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(std::make_unique<int>(43));
    })
    .detach();

  value = future.take().value();

  CHECK(*value == 43);
}
