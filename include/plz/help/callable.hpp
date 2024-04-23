#ifndef CALLABLE_HPP
#define CALLABLE_HPP

// Shamelessly copied from:
// https://codereview.stackexchange.com/questions/14730/impossibly-fast-delegate-in-c11
// I have made some minor modifications to the code.

#include <cassert>

#include <memory>

#include <new>

#include <type_traits>

#include <utility>

namespace plz
{

template <typename T>
class callable;

template <class R, class... A>
class callable<R(A...)>
{
  using stub_ptr_type = R (*)(void*, A&&...);

  callable(void* const o, stub_ptr_type const m) noexcept
    : m_object_ptr(o), m_stub_ptr(m)
  {
  }

  public:
  callable() = default;

  callable(callable const&) = default;

  callable(callable&&) = default;

  callable(std::nullptr_t const) noexcept : callable()
  {
  }

  template <class C>
    requires(std::is_class_v<C>)
  explicit callable(C const* const o) noexcept : m_object_ptr(const_cast<C*>(o))
  {
  }

  template <class C>
    requires(std::is_class_v<C>)
  explicit callable(C const& o) noexcept : m_object_ptr(const_cast<C*>(&o))
  {
  }

  template <class C>
  callable(C* const object_ptr, R (C::*const method_ptr)(A...))
  {
    *this = from(object_ptr, method_ptr);
  }

  template <class C>
  callable(C* const object_ptr, R (C::*const method_ptr)(A...) const)
  {
    *this = from(object_ptr, method_ptr);
  }

  template <class C>
  callable(C& object, R (C::*const method_ptr)(A...))
  {
    *this = from(object, method_ptr);
  }

  template <class C>
  callable(C const& object, R (C::*const method_ptr)(A...) const)
  {
    *this = from(object, method_ptr);
  }

  template <typename T>
    requires(!std::is_same_v<callable, typename std::decay<T>::type>)
  callable(T&& f)
    : m_store(operator new(sizeof(typename std::decay<T>::type)),
        functor_deleter<typename std::decay<T>::type>),
      m_store_size(sizeof(typename std::decay<T>::type))
  {
    using functor_type = typename std::decay<T>::type;

    new(m_store.get()) functor_type(std::forward<T>(f));

    m_object_ptr = m_store.get();

    m_stub_ptr = functor_stub<functor_type>;

    m_deleter = deleter_stub<functor_type>;
  }

  callable& operator=(callable const&) = default;

  callable& operator=(callable&&) = default;

  template <class C>
  callable& operator=(R (C::*const rhs)(A...))
  {
    return *this = from(static_cast<C*>(m_object_ptr), rhs);
  }

  template <class C>
  callable& operator=(R (C::*const rhs)(A...) const)
  {
    return *this = from(static_cast<C const*>(m_object_ptr), rhs);
  }

  template <typename T>
    requires(!std::is_same_v<callable, typename std::decay<T>::type>)
  callable& operator=(T&& f)
  {
    using functor_type = typename std::decay<T>::type;

    if((sizeof(functor_type) > m_store_size) || m_store.use_count() > 1)
    {
      m_store.reset(operator new(sizeof(functor_type)), functor_deleter<functor_type>);

      m_store_size = sizeof(functor_type);
    }
    else
    {
      m_deleter(m_store.get());
    }

    new(m_store.get()) functor_type(std::forward<T>(f));

    m_object_ptr = m_store.get();

    m_stub_ptr = functor_stub<functor_type>;

    m_deleter = deleter_stub<functor_type>;

    return *this;
  }

  template <R (*const function_ptr)(A...)>
  static callable from() noexcept
  {
    return { nullptr, function_stub<function_ptr> };
  }

  template <class C, R (C::*const method_ptr)(A...)>
  static callable from(C* const object_ptr) noexcept
  {
    return { object_ptr, method_stub<C, method_ptr> };
  }

  template <class C, R (C::*const method_ptr)(A...) const>
  static callable from(C const* const object_ptr) noexcept
  {
    return { const_cast<C*>(object_ptr), const_method_stub<C, method_ptr> };
  }

  template <class C, R (C::*const method_ptr)(A...)>
  static callable from(C& object) noexcept
  {
    return { &object, method_stub<C, method_ptr> };
  }

  template <class C, R (C::*const method_ptr)(A...) const>
  static callable from(C const& object) noexcept
  {
    return { const_cast<C*>(&object), const_method_stub<C, method_ptr> };
  }

  template <typename T>
  static callable from(T&& f)
  {
    return std::forward<T>(f);
  }

  static callable from(R (*const function_ptr)(A...))
  {
    return function_ptr;
  }

  template <class C>
  using member_pair = std::pair<C* const, R (C::*const)(A...)>;

  template <class C>
  using const_member_pair = std::pair<C const* const, R (C::*const)(A...) const>;

  template <class C>
  static callable from(C* const object_ptr, R (C::*const method_ptr)(A...))
  {
    return member_pair<C>(object_ptr, method_ptr);
  }

  template <class C>
  static callable from(C const* const object_ptr, R (C::*const method_ptr)(A...) const)
  {
    return const_member_pair<C>(object_ptr, method_ptr);
  }

  template <class C>
  static callable from(C& object, R (C::*const method_ptr)(A...))
  {
    return member_pair<C>(&object, method_ptr);
  }

  template <class C>
  static callable from(C const& object, R (C::*const method_ptr)(A...) const)
  {
    return const_member_pair<C>(&object, method_ptr);
  }

  void reset()
  {
    m_stub_ptr = nullptr;
    m_store.reset();
  }

  void reset_stub() noexcept
  {
    m_stub_ptr = nullptr;
  }

  void swap(callable& other) noexcept
  {
    std::swap(*this, other);
  }

  bool operator==(callable const& rhs) const noexcept
  {
    return (m_object_ptr == rhs.object_ptr_) && (m_stub_ptr == rhs.stub_ptr_);
  }

  bool operator!=(callable const& rhs) const noexcept
  {
    return !operator==(rhs);
  }

  bool operator<(callable const& rhs) const noexcept
  {
    return (m_object_ptr < rhs.object_ptr_) ||
      ((m_object_ptr == rhs.object_ptr_) && (m_stub_ptr < rhs.stub_ptr_));
  }

  bool operator==(std::nullptr_t const) const noexcept
  {
    return !m_stub_ptr;
  }

  bool operator!=(std::nullptr_t const) const noexcept
  {
    return m_stub_ptr;
  }

  explicit operator bool() const noexcept
  {
    return m_stub_ptr;
  }

  R operator()(A... args) const
  {
    //  assert(stub_ptr);
    return m_stub_ptr(m_object_ptr, std::forward<A>(args)...);
  }

  private:
  friend struct std::hash<callable>;

  using deleter_type = void (*)(void*);

  void* m_object_ptr;
  stub_ptr_type m_stub_ptr{};

  deleter_type m_deleter;

  std::shared_ptr<void> m_store;
  std::size_t m_store_size;

  template <class T>
  static void functor_deleter(void* const p)
  {
    static_cast<T*>(p)->~T();

    operator delete(p);
  }

  template <class T>
  static void deleter_stub(void* const p)
  {
    static_cast<T*>(p)->~T();
  }

  template <R (*function_ptr)(A...)>
  static R function_stub(void* const, A&&... args)
  {
    return function_ptr(std::forward<A>(args)...);
  }

  template <class C, R (C::*method_ptr)(A...)>
  static R method_stub(void* const object_ptr, A&&... args)
  {
    return (static_cast<C*>(object_ptr)->*method_ptr)(std::forward<A>(args)...);
  }

  template <class C, R (C::*method_ptr)(A...) const>
  static R const_method_stub(void* const object_ptr, A&&... args)
  {
    return (static_cast<C const*>(object_ptr)->*method_ptr)(std::forward<A>(args)...);
  }

  template <typename>
  struct is_member_pair : std::false_type
  {
  };

  template <class C>
  struct is_member_pair<std::pair<C* const, R (C::*const)(A...)>> : std::true_type
  {
  };

  template <typename>
  struct is_const_member_pair : std::false_type
  {
  };

  template <class C>
  struct is_const_member_pair<std::pair<C const* const, R (C::*const)(A...) const>>
    : std::true_type
  {
  };

  template <typename T>
    requires(!(is_member_pair<T>::value || is_const_member_pair<T>::value))
  static R functor_stub(void* const object_ptr, A&&... args)
  {
    return (*static_cast<T*>(object_ptr))(std::forward<A>(args)...);
  }

  template <typename T>
    requires((is_member_pair<T>::value || is_const_member_pair<T>::value))
  static R functor_stub(void* const object_ptr, A&&... args)
  {
    return (static_cast<T*>(object_ptr)->first->*static_cast<T*>(object_ptr)->second)(
      std::forward<A>(args)...);
  }
};

} // namespace plz
namespace std
{
template <typename R, typename... A>
struct hash<plz::callable<R(A...)>>
{
  size_t operator()(plz::callable<R(A...)> const& d) const noexcept
  {
    auto const seed(hash<void*>()(d.object_ptr_));

    return hash<typename plz::callable<R(A...)>::stub_ptr_type>()(d.stub_ptr_) +
      0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
};
} // namespace std

#endif // CALLABLE_HPP
