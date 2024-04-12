#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <list>
#include <vector>

#include <plz/help/type_traits.hpp>

using namespace plz;

TEST_CASE(
  "type_traits: is_specialization should return true for specialization")
{
  using TestType = is_specialization_of<std::vector<int>, std::vector>;
  REQUIRE(TestType::value == true);
}

TEST_CASE("type_traits: is_specialization should return false for "
          "non-specialization")
{
  using TestType = is_specialization_of<int, std::vector>;
  REQUIRE(TestType::value == false);
}

TEST_CASE("type_traits: is_specialization should return false for "
          "non-matching specialization")
{
  using TestType = is_specialization_of<std::list<float>, std::vector>;
  REQUIRE(TestType::value == false);
}

TEST_CASE("type_traits: is_specialization should return true for "
          "specialization with "
          "multiple arguments")
{
  using TestType = is_specialization_of<std::pair<int, double>, std::pair>;
  REQUIRE(TestType::value == true);
}

TEST_CASE("type_traits: is_specialization should return true for "
          "specialization with no arguments")
{
  using TestType = is_specialization_of<std::tuple<>, std::tuple>;
  REQUIRE(TestType::value == true);
}

TEST_CASE("type_traits: get_template_arg_type retrieves arguments from a "
          "template specialization")
{
  using TestType = get_template_arg_type_of<std::vector<int>>;

  SECTION("Check number of arguments")
  {
    REQUIRE(TestType::nargs == 2);
  }

  SECTION("Check type of the first argument")
  {
    REQUIRE(std::is_same_v<TestType::arg<0>::type, int>);
  }
}

TEST_CASE(
  "type_traits: get_template_arg_type handles multiple template arguments")
{
  using TestType = get_template_arg_type_of<std::pair<double, char>>;

  SECTION("Check number of arguments")
  {
    REQUIRE(TestType::nargs == 2);
  }

  SECTION("Check type of the first argument")
  {
    REQUIRE(std::is_same_v<TestType::arg<0>::type, double>);
  }

  SECTION("Check type of the second argument")
  {
    REQUIRE(std::is_same_v<TestType::arg<1>::type, char>);
  }
}

template <typename T1, typename T2, typename T3>
class CustomClass
{
};

TEST_CASE("type_traits: get_template_arg_type works with custom template class")
{
  using TestType = get_template_arg_type_of<CustomClass<int, double, float>>;

  SECTION("Check number of arguments")
  {
    REQUIRE(TestType::nargs == 3);
  }

  SECTION("Check type of the third argument")
  {
    REQUIRE(std::is_same_v<TestType::arg<2>::type, float>);
  }
}

TEST_CASE("type_traits: function_traits retrieves return type and "
          "argument types", )
{
  auto lambda = [](int x, double y)
  {
    return x + y;
  };

  using LambdaTraits = function_traits<decltype(lambda)>;

  SECTION("[function_traits] Check return type")
  {
    REQUIRE(std::is_same_v<LambdaTraits::return_type, double>);
  }

  SECTION("Check number of arguments")
  {
    REQUIRE(LambdaTraits::nargs == 2);
  }

  SECTION("Check type of the second argument")
  {
    REQUIRE(std::is_same_v<LambdaTraits::arg<1>::type, double>);
  }
}

TEST_CASE("type_traits: function_traits works with regular functions")
{
  using FunctionTraits = function_traits<int(float, char)>;

  SECTION("Check return type")
  {
    REQUIRE(std::is_same_v<FunctionTraits::return_type, int>);
  }

  SECTION("Check number of arguments")
  {
    REQUIRE(FunctionTraits::nargs == 2);
  }

  SECTION("Check type of the first argument")
  {
    REQUIRE(std::is_same_v<FunctionTraits::arg<0>::type, float>);
  }
}

TEST_CASE("type_traits: function_traits works with function pointers")
{
  using FunctionPtr    = int (*)(double, char);
  using FunctionTraits = function_traits<FunctionPtr>;

  SECTION("Check return type")
  {
    REQUIRE(std::is_same_v<FunctionTraits::return_type, int>);
  }

  SECTION("Check number of arguments")
  {
    REQUIRE(FunctionTraits::nargs == 2);
  }

  SECTION("Check type of the second argument")
  {
    REQUIRE(std::is_same_v<FunctionTraits::arg<1>::type, char>);
  }
}

TEST_CASE("type_traits: function_traits works with member function pointers")
{
  struct MyClass
  {
    int memberFunction(double, char) const
    {
      return 42;
    }
  };

  using MemberFunctionPtr = int (MyClass::*)(double, char) const;
  using FunctionTraits    = function_traits<MemberFunctionPtr>;

  SECTION("Check return type")
  {
    REQUIRE(std::is_same_v<FunctionTraits::return_type, int>);
  }

  SECTION("Check number of arguments")
  {
    REQUIRE(FunctionTraits::nargs == 2);
  }

  SECTION("Check type of the second argument")
  {
    REQUIRE(std::is_same_v<FunctionTraits::arg<1>::type, char>);
  }
}
