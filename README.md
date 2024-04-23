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
## multiple futures/promises

plz::futures provide a convenient way to handle a collection of future/promise objects using a key type and provide an aggregate result handle using a plz::future.

```cpp
  std::array promises = {
    plz::promise<double>(), plz::promise<double>(), plz::promise<double>()
  };

  auto futures = plz::make_futures<double, int>();

  for(int i = 0; i < promises.size(); ++i)
  {
    futures.add_future(i, promises[i].get_future());

    std::thread(
      [i, promise = promises[i]]() mutable
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise.set_result(i);
      })
      .detach();
  }

  double res1 = futures.get(0);
  double res2 = futures.get(1);
  double res3 = futures.get(2);

  assert(res1 == 0);
  assert(res2 == 1);
  assert(res3 == 2);

  std::vector<double> res = futures.get();
  double acc              = std::accumulate(res.begin(), res.end(), 0.0);
  
  assert(acc == promises.size() * (promises.size() - 1) / 2);
```

## thread_pool
`plz::thread_pool` class is a fixed size thread pool. The user submits tasks (any callable object) to be executed into a queue using the `run` method, which return a `plz::future` object. 
```cpp
plz::thread_pool pool(3);

auto task = [](int x)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return x;
};

pool.run(task, 84);
auto future = pool.run(task, 42);

int result = future.get();

assert(result == 42);
```
plz::thread_pool uses instances of std::jthreads. So the user can enqueue a callable object that take a std::stop_token as its last argument called with workers jthread stop token for gracefull task cancellation when the the jthread tries to stop.

```cpp
plz::thread_pool pool(3);

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
```

The user can also enqueue a task to be called on range of values using the `map` method which in turn will return a `plz::futures` object.

```cpp
plz::thread_pool pool(4);

std::array vec = { 1, 2, 3, 4, 5, 6, 7, 8 };
auto f         = pool.map(vec, [] (auto i) { return i + 1; });

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
              .get();

assert(sum == 44);
```
A singleton global instance of plz::thread_pool is available and can be used directly using the free functions under the namespace plz:

```cpp
plz::set_thread_count(4); // will throw if global instance is already configured with number of threads.
                          // So this function should be called before any usage of the global isntance of plz::thread_pool.
                          // Otherwise the default number of threads is gonna be std::thread::hardware_concurrency()

auto foo = [] (int i, int x) -> int
{
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  return i + x;
}

plz::run(foo, 1, 2).get(); // enqueue foo on the global instance plz::thread_pool::global_instance() and wait for the result


std::array vec = { 1, 2, 3, 4, 5, 6, 7, 8 };
std::vector<int> res_vec = plz::map(vec, [] (auto i) { return i + 1; }).get(); // enqueue the lambda for each element in the provided range and wait for the aggregate result

```

`plz::future` has a `then` overload that takes an isntance of `plz::thread_pool` as first argument. This overload will enquue the provided callable on the provided thread pool. `async_then` method on the other hand will enqueue the task on the same thread_pool without having to specifiy it as argument

```cpp
plz::thread_pool pool(2);
plz::thread_pool pool2(2);

auto foo = [] (int i, int x) -> int
{
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  return i + x;
}

// enqueue foo on the pool then enqueue a lambda on the second pool
pool.run(foo, 1, 2)
  .then(
    &pool2, 
    [](int result) 
    { 
      return result - 1;
    }
  ); 

// enqueue foo on the pool then enqueue a lambda on the same pool
pool.run(foo, 1, 2).
  async_then(
    [](int result)
    { 
      return result - 1;
    }
); 

// wait for the pools to be idle
pool.wait();
pool2.wait();
```
## circular buffer reader and writer
// TODO