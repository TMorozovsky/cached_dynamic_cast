#include "../cached_dynamic_cast/cached_dynamic_cast.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <iostream>
#include <chrono>

namespace
{
  class test_failed_exception final : public std::exception
  {
  public:
    explicit test_failed_exception(int line)
      : error_text{ std::string{"test failed at line "} + std::to_string(line) + ", file " + __FILE__ }
    {
    }

    [[nodiscard]] const char* what() const noexcept override
    {
      return error_text.c_str();
    }

  private:
    std::string error_text;
  };

#define THROW_TEST_FAILED() throw ::test_failed_exception{__LINE__};

#define ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(result_pointer_expression, expected_typeid_class) \
  { \
    auto* result_pointer = (result_pointer_expression); \
    if (result_pointer == nullptr) \
      THROW_TEST_FAILED(); \
    if (typeid(*result_pointer) != typeid(expected_typeid_class)) \
      THROW_TEST_FAILED(); \
  }

#define ASSERT_NULL(result_pointer_expression) \
  { \
    auto* result_pointer = (result_pointer_expression); \
    if (result_pointer != nullptr) \
      THROW_TEST_FAILED(); \
  }

#define ASSERT_HAS_TYPEID_OF(result_reference_expression, expected_typeid_class) \
  { \
    auto& result_reference = (result_reference_expression); \
    if (typeid(result_reference) != typeid(expected_typeid_class)) \
      THROW_TEST_FAILED(); \
  }

#define ASSERT_THROWS_BAD_CAST(result_reference_expression) \
  { \
    bool unexpected_successful_cast = false; \
    try \
    { \
      (void)(result_reference_expression); \
      unexpected_successful_cast = true; \
    } \
    catch (const std::bad_cast&) \
    { \
    } \
    if (unexpected_successful_cast) \
      THROW_TEST_FAILED(); \
  }

template<std::size_t NumberOfBytes>
struct DummyOffsetModifyingStruct
{
public:
  virtual ~DummyOffsetModifyingStruct() = default;

private:
  static_assert(NumberOfBytes > sizeof(void*)); // to compensate the vtable pointer
  static_assert(NumberOfBytes % sizeof(void*) == 0);
  std::array<unsigned char, NumberOfBytes - sizeof(void*)> dummy_offset_modifying_data{};
};

// simple base and derived classes
class SimpleBase : public DummyOffsetModifyingStruct<24>
{
public:
  virtual ~SimpleBase() = default;
};

class SimpleDerived : public DummyOffsetModifyingStruct<48>, public SimpleBase, public DummyOffsetModifyingStruct<56>
{
};

class SimpleDerivedFromDerived : public DummyOffsetModifyingStruct<80>, public SimpleDerived, public DummyOffsetModifyingStruct<96>
{
};

class OtherSimpleDerived : public DummyOffsetModifyingStruct<64>, public SimpleBase, public DummyOffsetModifyingStruct<72>
{
};

class OtherSimpleDerivedFinal final : public DummyOffsetModifyingStruct<72>, public SimpleBase, public DummyOffsetModifyingStruct<64>
{
};

// virtual inheritance hierarchy
class A : public DummyOffsetModifyingStruct<40>
{
public:
  virtual ~A() = default;
};

class B : public virtual DummyOffsetModifyingStruct<48>, public virtual A, public virtual DummyOffsetModifyingStruct<56>
{
};

class C : public virtual DummyOffsetModifyingStruct<64>, public virtual A, public virtual DummyOffsetModifyingStruct<72>
{
};

class D : public DummyOffsetModifyingStruct<80>, public B, public DummyOffsetModifyingStruct<96>, public C, public DummyOffsetModifyingStruct<104>
{
};

static void reset_global_cache()
{
  std::unique_lock writer_lock{ detail::cached_dynamic_cast_detail::global_cache_mutex };
  detail::cached_dynamic_cast_detail::global_cache.clear();
}

static void static_tests()
{
  reset_global_cache();

  B object;

  A* object_non_const_pointer = &object;
  const A* object_const_pointer = &object;

  A& object_non_const_reference = object;
  const A& object_const_reference = object;

  [[maybe_unused]] auto&& result_of_non_const_pointer_cast_to_non_const = cached_dynamic_cast<B*>(object_non_const_pointer);
  static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_non_const_pointer_cast_to_non_const)>, B*>);

  [[maybe_unused]] auto&& result_of_non_const_pointer_cast_to_const = cached_dynamic_cast<const B*>(object_non_const_pointer);
  static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_non_const_pointer_cast_to_const)>, const B*>);

  // should not compile
  //[[maybe_unused]] auto&& result_of_const_pointer_cast_to_non_const = cached_dynamic_cast<B*>(object_const_pointer);
  //static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_const_pointer_cast_to_non_const)>, B*>);

  [[maybe_unused]] auto&& result_of_const_pointer_cast_to_const = cached_dynamic_cast<const B*>(object_const_pointer);
  static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_const_pointer_cast_to_const)>, const B*>);

  [[maybe_unused]] auto&& result_of_non_const_reference_cast_to_non_const = cached_dynamic_cast<B&>(object_non_const_reference);
  static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_non_const_reference_cast_to_non_const)>, B>);

  [[maybe_unused]] auto&& result_of_non_const_reference_cast_to_const = cached_dynamic_cast<const B&>(object_non_const_reference);
  static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_non_const_reference_cast_to_const)>, const B>);

  // should not compile
  //[[maybe_unused]] auto&& result_of_const_reference_cast_to_non_const = cached_dynamic_cast<B&>(object_const_reference);
  //static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_const_reference_cast_to_non_const)>, B>);

  [[maybe_unused]] auto&& result_of_const_reference_cast_to_const = cached_dynamic_cast<const B&>(object_const_reference);
  static_assert(std::is_same_v<std::remove_reference_t<decltype(result_of_const_reference_cast_to_const)>, const B>);

  reset_global_cache();
}

static void test_01() // cast from a base type to a derived type, a valid cast, different dynamic `typeid`s
{
  reset_global_cache();

  SimpleDerivedFromDerived derived;
  SimpleBase* base_pointer = &derived;

  // the first call should add a new entry to the cache, the second call should reuse it
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(base_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(base_pointer), SimpleDerivedFromDerived);

  // this should reuse the cache as well
  ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived&>(*base_pointer), SimpleDerivedFromDerived);
}

static void test_02() // cast from a base type to a derived type, a valid cast, same dynamic `typeid`
{
  reset_global_cache();

  SimpleDerived derived;
  SimpleBase* base_pointer = &derived;

  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(base_pointer), SimpleDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(base_pointer), SimpleDerived);

  ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived&>(*base_pointer), SimpleDerived);
}

static void test_03() // cast from a base type to a derived type, an invalid cast
{
  reset_global_cache();

  SimpleDerived other_derived;
  SimpleBase* base_pointer = &other_derived;

  ASSERT_NULL(cached_dynamic_cast<OtherSimpleDerived*>(base_pointer));
  ASSERT_NULL(cached_dynamic_cast<OtherSimpleDerived*>(base_pointer));

  ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<OtherSimpleDerived&>(*base_pointer));
}

static void test_04() // cast between the same static types, different dynamic `typeid`s
{
  reset_global_cache();

  SimpleDerivedFromDerived object;
  SimpleDerived* object_pointer = &object;

  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(object_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(object_pointer), SimpleDerivedFromDerived);

  ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived&>(*object_pointer), SimpleDerivedFromDerived);
}

static void test_05() // cast between the same static types, same dynamic `typeid`
{
  reset_global_cache();

  SimpleDerived object;
  SimpleDerived* object_pointer = &object;

  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(object_pointer), SimpleDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived*>(object_pointer), SimpleDerived);

  ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerived&>(*object_pointer), SimpleDerived);
}

static void test_06() // cast from a derived type to a base type, different dynamic `typeid`s
{
  reset_global_cache();

  SimpleDerivedFromDerived derived;
  SimpleDerived* derived_pointer = &derived;

  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleBase*>(derived_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleBase*>(derived_pointer), SimpleDerivedFromDerived);

  ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<SimpleBase&>(*derived_pointer), SimpleDerivedFromDerived);
}

static void test_07() // cast from a derived type to a base type, same dynamic `typeid`
{
  reset_global_cache();

  SimpleDerived derived;
  SimpleDerived* derived_pointer = &derived;

  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleBase*>(derived_pointer), SimpleDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleBase*>(derived_pointer), SimpleDerived);

  ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<SimpleBase&>(*derived_pointer), SimpleDerived);
}

static void test_08() // cast between completely unrelated types
{
  reset_global_cache();

  SimpleDerived object;
  SimpleDerived* object_pointer = &object;

  ASSERT_NULL(cached_dynamic_cast<B*>(object_pointer));
  ASSERT_NULL(cached_dynamic_cast<B*>(object_pointer));

  ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<B&>(*object_pointer));
}

static void test_09() // "diamond" hierarchy with virtual inheritance, various casts
{
  if (reset_global_cache(); true) // destination type = A, source STATIC type = A, source DYNAMIC type = A
  {
    A object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), A);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), A);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), A);
  }

  if (reset_global_cache(); true) // destination type = A, source STATIC type = A, source DYNAMIC type = B
  {
    B object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), B);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), B);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), B);
  }

  if (reset_global_cache(); true) // destination type = A, source STATIC type = A, source DYNAMIC type = C
  {
    C object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), C);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), C);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), C);
  }

  if (reset_global_cache(); true) // destination type = A, source STATIC type = A, source DYNAMIC type = D
  {
    D object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = A, source STATIC type = B, source DYNAMIC type = B
  {
    B object;
    B* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), B);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), B);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), B);
  }

  if (reset_global_cache(); true) // destination type = A, source STATIC type = B, source DYNAMIC type = D
  {
    D object;
    B* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = A, source STATIC type = C, source DYNAMIC type = C
  {
    C object;
    C* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), C);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), C);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), C);
  }

  if (reset_global_cache(); true) // destination type = A, source STATIC type = C, source DYNAMIC type = D
  {
    D object;
    C* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<A*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<A&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = B, source STATIC type = A, source DYNAMIC type = A
  {
    A object;
    A* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<B*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<B*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<B&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = B, source STATIC type = A, source DYNAMIC type = B
  {
    B object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<B*>(object_pointer), B);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<B*>(object_pointer), B);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<B&>(*object_pointer), B);
  }

  if (reset_global_cache(); true) // destination type = B, source STATIC type = A, source DYNAMIC type = C
  {
    C object;
    A* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<B*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<B*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<B&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = B, source STATIC type = A, source DYNAMIC type = D
  {
    D object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<B*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<B*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<B&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = C, source STATIC type = A, source DYNAMIC type = A
  {
    A object;
    A* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<C*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<C*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<C&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = C, source STATIC type = A, source DYNAMIC type = B
  {
    B object;
    A* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<C*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<C*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<C&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = C, source STATIC type = A, source DYNAMIC type = C
  {
    C object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<C*>(object_pointer), C);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<C*>(object_pointer), C);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<C&>(*object_pointer), C);
  }

  if (reset_global_cache(); true) // destination type = C, source STATIC type = A, source DYNAMIC type = D
  {
    D object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<C*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<C*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<C&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = A, source DYNAMIC type = A
  {
    A object;
    A* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<D&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = A, source DYNAMIC type = B
  {
    B object;
    A* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<D&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = A, source DYNAMIC type = C
  {
    C object;
    A* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<D&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = A, source DYNAMIC type = D
  {
    D object;
    A* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<D&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = B, source DYNAMIC type = B
  {
    B object;
    B* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<D&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = B, source DYNAMIC type = D
  {
    D object;
    B* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<D&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = C, source DYNAMIC type = C
  {
    C object;
    C* object_pointer = &object;
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<D*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<D&>(*object_pointer));
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = C, source DYNAMIC type = D
  {
    D object;
    C* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<D&>(*object_pointer), D);
  }

  if (reset_global_cache(); true) // destination type = D, source STATIC type = D, source DYNAMIC type = D
  {
    D object;
    D* object_pointer = &object;
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<D*>(object_pointer), D);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<D&>(*object_pointer), D);
  }
}

static void test_10() // special case: cast from `nullptr`
{
  SimpleDerived* const object_pointer = nullptr;

  if (reset_global_cache(); true) // cast to a base class
  {
    ASSERT_NULL(cached_dynamic_cast<SimpleBase*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<SimpleBase*>(object_pointer));
  }

  if (reset_global_cache(); true) // cast to the same class
  {
    ASSERT_NULL(cached_dynamic_cast<SimpleDerived*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<SimpleDerived*>(object_pointer));
  }

  if (reset_global_cache(); true) // cast to a derived class
  {
    ASSERT_NULL(cached_dynamic_cast<SimpleDerivedFromDerived*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<SimpleDerivedFromDerived*>(object_pointer));
  }
}

static void test_11() // special case: cast to a `final` class
{
  if (reset_global_cache(); true) // successful scenario
  {
    OtherSimpleDerivedFinal object;
    SimpleBase* object_pointer = &object;

    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<OtherSimpleDerivedFinal*>(object_pointer), OtherSimpleDerivedFinal);
    ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<OtherSimpleDerivedFinal*>(object_pointer), OtherSimpleDerivedFinal);
    ASSERT_HAS_TYPEID_OF(cached_dynamic_cast<OtherSimpleDerivedFinal&>(*object_pointer), OtherSimpleDerivedFinal);
  }

  if (reset_global_cache(); true) // unsuccessful scenario
  {
    SimpleDerived object;
    SimpleBase* object_pointer = &object;

    ASSERT_NULL(cached_dynamic_cast<OtherSimpleDerivedFinal*>(object_pointer));
    ASSERT_NULL(cached_dynamic_cast<OtherSimpleDerivedFinal*>(object_pointer));
    ASSERT_THROWS_BAD_CAST(cached_dynamic_cast<OtherSimpleDerivedFinal&>(*object_pointer));
  }
}

static void test_12() // sequentially try casting from different "static" types that have the same "dynamic" type
{
  reset_global_cache();

  SimpleDerivedFromDerived object;

  SimpleDerivedFromDerived* most_derived_pointer = &object;
  SimpleDerived* middle_pointer = &object;
  SimpleBase* base_pointer = &object;

  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(most_derived_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(base_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(middle_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(most_derived_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(base_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(middle_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(most_derived_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(base_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(middle_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(most_derived_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(base_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(middle_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(most_derived_pointer), SimpleDerivedFromDerived);
  ASSERT_NOT_NULL_AND_HAS_TYPEID_OF(cached_dynamic_cast<SimpleDerivedFromDerived*>(base_pointer), SimpleDerivedFromDerived);
}

static int run_all_tests()
{
  try
  {
    test_01();
    test_02();
    test_03();
    test_04();
    test_05();
    test_06();
    test_07();
    test_08();
    test_09();
    test_10();
    test_11();
    test_12();
    return 0;
  }
  catch (const test_failed_exception& ex)
  {
    std::cout << ex.what() << '\n';
    return 1;
  }
}

int run_all_tests_multiple_times()
{
  std::cout << "starting..." << '\n';
  int result = 0;
  const int times = 10'000;
  auto t_begin = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < times; ++i)
    result += run_all_tests();
  auto t_end = std::chrono::high_resolution_clock::now();
  std::cout << "all runs finished in " << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count() << " milliseconds" << '\n';
  return result;
}
} // unnamed namespace

int main()
{
  static_tests();
  return run_all_tests_multiple_times();
}
