# cplease
CppParallelEase (or cplease) is a C++ header-only library that tries to provide some improved primitives to ease the writing of parallel and asynchronous c++ tasks

## future/promise

The pair of `plz::future` and `plz::promise` classes are similiar to `std::promise`
and `std::future`. They provide a mechanism to to access the result of asynchronous operations.

The creator of the promise class can set a value
using the `set_result()` or set an exception of any type using `set_exception()`. The
associated future object can retrieve the value using the `get()` method which
will block the calling thread until the promise is fulfilled. The `take()` method can also be used to move out the result from the shared state of promise/future pair. If an exception is thrown during the computation, the `get()` methods will rethrow the exception.

The future class also provides a `then()` method that allows the user to attach
an arbitray continuation to the result of the future, which will be executed
when the result is ready. The `then()` method returns a new future that
represents the result of the continuation.
The future class also provides an `on_exception()` method that
allows the user to attach an exception handler to the future, which will be
executed when an exception is thrown during the computation.

The exception handler can be a function that takes a `std::exception_ptr` as an
argument which will handle any type of exception, or a function that takes a
specific exception type as an argument which will be invoked if that specific
expection was set on the promise. The `on_exception()` method returns a
reference to the future object, so that multiple exception handlers can be
attached to the same future.

### Usage example

```cpp
#include <plz/future.hpp>

// simulate a asynchronous task that return a plz::future object as handler for the result
auto some_async_task() -> plz::future<int>
{
  plz::promise<int> promise;
  plz::future<int> future = promise.get_future();

  std::thread(
    [promise = std::move(promise)]()
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try
      {
        promise.set_result(42);

        // an arbitrary type can be set as an exception
        // promise.set_exception(42);
        // promise.set_exception(std::runtime_error("error"));
      }
      catch(...)
      {
        // std::exception_ptr can be set as a type of exception
        promise.set_exception(std::current_exception());
      }
    })
    .detach();

  return future;
}

int main()
{
  // block the calling the thread until the promise is fulfilled. If an exception was set on the promise, it will be rethrown here
  int result = some_async_task().get();
  assert(result == 42);

  // similair to `get`, but the result is moved out and the promise is reset to the "not ready" state
  int result2 = some_async_task().take();
  assert(result2 == 42);

  // chain the async task with a lambda that takes the result of the previous task and returns a new result, then wait for the final result
  int result3 = some_async_task()
                  .then(
                    [](int result)
                    {
                      return std::to_string(result);
                    })
                  .then(
                    [](std::string result)
                    {
                      return std::stoi(result);
                    })
                  .get();
  assert(result3 == 42);

  // Catch the exception thrown in then wait for the final result. If an exception was set on the promise, it will be rethrown here
  try
  {
    int result4 = some_async_task()
                    .then(
                      [](int result)
                      {
                        // At any moment in the execution of the chained continuations,
                        // a thrown exception will be captured and set on the promise and will be rethrown when calling `wait`
                        throw 42;
                        return std::to_string(result);
                      })
                    .then(
                      [](std::string result)
                      {
                        return std::stoi(result);
                      })
                    .get();
  }
  catch(int& e)
  {
    assert(e == 42);
  }

  // Install a handler that will be called when an exception is thrown in the chain of continuations
  // The exception will still be rethrown when calling `wait`
  try
  {
    int result5 = some_async_task()
                    .then(
                      [](int result)
                      {
                        throw std::runtime_error("error");
                        return result;
                      })
                    .then(
                      [](int result)
                      {
                        // this lambda will not be called
                        assert(false);
                        return result;
                      })
                    .on_exception(
                      [](std::runtime_error& e)
                      {
                        // this will be called when the exception of the same type is thrown
                      })
                    .on_exception(
                      [](std::exception& e)
                      {
                        // this will not be called since the exception is handled in the previous `on_exception` handler
                      })
                    .get();
  }
  catch(std::runtime_error& e)
  {
    assert(std::string(e.what()) == "error");
  }

  // Chain another async task with a lambda that return a plz::future object
  int result6 = some_async_task()
                  .then(
                    [](int result)
                    {
                      // make_promise is equivalent to just declaring plz::promise normally
                      auto promise = plz::make_promise<int>();
                      auto future = promise.get_future();
                      std::thread(
                        [promise = std::move(promise)]()
                        {
                          promise.set_result(result + 1);
                        })
                        .detach();
                      return future;
                    })
                  .then(
                    [](int result)
                    {
                      return result;
                    })
                  .get();
  assert(result6 == 43);
}
``` 

### specializations for std::expected

If a c++23 compiler is used with support for std::expected, adding the define "WITH_STD_EXPECTED" will enable the template classes plz::expected::future and plz::expected::promise. Similiar to std::expected, these classes takes first template parameter `T` for the value and a second `E` is the type of the error. 
These classes does not support the catching of rethrowing of exceptions. The user should handle the exceptions himself.(i.e catching the exception and converting it to error type.)

The return type of `get` ant `take` methods is `std::expected<T,E>`

The template function `plz::make_promise<T>()` create a `promise<T>` and template function `plz::make_promise<T, E>()` create a `plz::expected::promise<T, E>`.

```cpp
#define WITH_STD_EXPECTED
#include <plz/future.hpp>

enum class ErrorCode
{
  error_1,
  error_2
};

// simulate a asynchronous task that return a plz::expected::future object as handler for the result
auto some_async_task() -> plz::expected::future<int, ErrorCode>
{
  auto promise = plz::make_promise<int, ErrorCode>;
  auto future  = promise.get_future();

  std::thread(
    [promise = std::move(promise)]()
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      promise.set_result(42);

      // eventually we can set an error:
      // promise.set_error(ErrorCode::error_1);
      // promise.set_result(std::unexpected(ErrorCode::error_1));
    })
    .detach();

  return future;
}

int main()
{
  // block the calling the thread until the promise is fulfilled.
  std::expected<int, ErrorCode> result = some_async_task().get();
  assert(result.value() == 42);

  // similair to `get`, but the result is moved out and the promise is reset to the "not ready" state
  std::expected<int, ErrorCode> result2 = some_async_task().take();
  assert(result2.value() == 42);

  // chain the async task with a lambda that takes the result of the previous task and returns a new result, then wait for the final result
  std::expected<int, ErrorCode> result3 = 
    some_async_task()
      .then(
        [](int result)
        {
          return std::to_string(result);
        })
      .then(
        [](std::string result)
        {
          return std::stoi(result);
        })
      .get()
      .value();
  assert(result3 == 42);

  // Install a handler that will be called when an expected is returned in the chain of continuations
  // The error will still be retrievable on the expected result object

  std::expected<int, ErrorCode> result5 =
    some_async_task()
      .then(
        [](int result) -> std::expected<int, ErrorCode>
        {
          return std::unexpected(ErrorCode::error_1);
        })
      .then(
        [](int result)
        {
          // this lambda will not be called
          assert(false);
          return result;
        })
      .on_error(
        [](ErrorCode e)
        {
          // this will be called when error is set on the promise
          assert(e == ErrorCode::error_1);
        })
      .get();

  assert(result5.error() == ErrorCode::error_1);
}
```

## packaged_tasks

Like `std::packaged_task` the class template `plz::packaged_task` wraps any Callable target (function, lambda expression, bind expression, or another function object) so that it can be invoked asynchronously. They are designed to work with `plz::future`, `plz::promise` and `plz::thread_pool` classes. 

```cpp

```

## thread_pool
TODO
## circular buffer
TODO