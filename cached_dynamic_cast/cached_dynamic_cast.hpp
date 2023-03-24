#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <cstddef>
#include <typeinfo>
#include <typeindex>
#include <type_traits>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace detail::cached_dynamic_cast_detail
{
  using offset_type = signed int; // could've been `std::ptrdiff_t`, but this should be enough in practice

  using global_cache_type =
    std::unordered_map<std::type_index /* destination STATIC type */,
                       std::unordered_map<std::type_index /* source DYNAMIC type */,
                                          std::pair<bool /* is the cast possible? */,
                                                    std::unordered_map<std::type_index /* source STATIC type */,
                                                                       offset_type>>>>;

  extern global_cache_type global_cache;
  extern std::shared_mutex global_cache_mutex;

  [[nodiscard]] inline offset_type checked_cast_to_offset(const std::ptrdiff_t wide_offset)
  {
    if ((wide_offset > static_cast<std::ptrdiff_t>(std::numeric_limits<offset_type>::min()))
     && (wide_offset < static_cast<std::ptrdiff_t>(std::numeric_limits<offset_type>::max())))
      return static_cast<offset_type>(wide_offset);
    else
      throw std::logic_error{"offset is too large"};
  }
} // namespace detail::cached_dynamic_cast_detail

inline void reset_cached_dynamic_cast_global_cache()
{
  std::unique_lock writer_lock{ detail::cached_dynamic_cast_detail::global_cache_mutex };
  detail::cached_dynamic_cast_detail::global_cache.clear();
}

// primary template: cast from a pointer type to a pointer type
template<typename DestinationPointer, typename SourcePointer>
[[nodiscard]] inline std::enable_if_t<std::is_pointer_v<DestinationPointer>, DestinationPointer>
cached_dynamic_cast(SourcePointer const source_pointer)
{
  static_assert(std::is_pointer_v<SourcePointer>); // casting to a pointer type is allowed from a pointer type only

  using SourceValue = std::remove_pointer_t<SourcePointer>;
  using DestinationValue = std::remove_pointer_t<DestinationPointer>;

  // adding cv-qualifiers is okay, but removing them is not
  static_assert(static_cast<int>(std::is_const_v<DestinationValue>)
             >= static_cast<int>(std::is_const_v<SourceValue>));
  static_assert(static_cast<int>(std::is_volatile_v<DestinationValue>)
             >= static_cast<int>(std::is_volatile_v<SourceValue>));

  using SourceValueNoCV = std::remove_cv_t<SourceValue>;
  using DestinationValueNoCV = std::remove_cv_t<DestinationValue>;

  // casting is supported for polymorphic class types only (at least for now)
  static_assert(std::is_polymorphic_v<SourceValueNoCV>);
  static_assert(std::is_polymorphic_v<DestinationValueNoCV>);

  // don't waste time if the types are actually the same
  // or if the source type is publicly derived from the destination type
  if constexpr (std::is_base_of_v<DestinationValueNoCV, SourceValueNoCV>
             && std::is_convertible_v<SourceValueNoCV*, DestinationValueNoCV*>)
    return source_pointer;

  // filter out the case where the client attempts to cast from a null pointer
  if (source_pointer == nullptr)
    return nullptr;

  const std::type_index destination_type{ typeid(DestinationValueNoCV) };
  const std::type_index source_dynamic_type{ typeid(*source_pointer) };

  // shortcut for casting to a `final` class
  if constexpr (std::is_final_v<DestinationValueNoCV>)
    if (source_dynamic_type != destination_type)
      return nullptr;

  const std::type_index source_static_type{ typeid(SourceValueNoCV) };

  // main logic of the cached dynamic cast from a non-null source pointer
  using namespace detail::cached_dynamic_cast_detail;
  if (std::shared_lock reader_lock{ global_cache_mutex }; true)
  {
    global_cache_type::const_iterator iter_destination_type = global_cache.find(destination_type);
    if (iter_destination_type != global_cache.end())
    {
      auto& map_source_dynamic_types = iter_destination_type->second;
      auto iter_source_dynamic_type = map_source_dynamic_types.find(source_dynamic_type);
      if (iter_source_dynamic_type != map_source_dynamic_types.end())
      {
        auto& [is_cast_possible, map_source_static_types] = iter_source_dynamic_type->second;
        if (is_cast_possible)
        {
          auto iter_source_static_type = map_source_static_types.find(source_static_type);
          if (iter_source_static_type != map_source_static_types.end())
          {
            const offset_type offset = iter_source_static_type->second;
            return const_cast<DestinationPointer>(
              reinterpret_cast<const volatile DestinationValueNoCV*>(
                reinterpret_cast<const volatile unsigned char*>(source_pointer) + offset));
          }
        }
        else
        {
          // the cast from the source DYNAMIC type to the destination type is impossible
          return nullptr;
        }
      }
    }
  }

  // if reached this line, there is no entry about the attempted cast in the global cache (yet):
  // perform a standard C++ dynamic_cast, then add an entry about its result to the cache
  DestinationPointer const destination_pointer = dynamic_cast<DestinationPointer>(source_pointer);

  const offset_type offset{
    (destination_pointer != nullptr)
    ? checked_cast_to_offset(
        reinterpret_cast<const volatile unsigned char*>(destination_pointer) -
        reinterpret_cast<const volatile unsigned char*>(source_pointer))
    : 0 /* this is invalid, but it will not be used anyway */
  };

  if (std::unique_lock writer_lock{ global_cache_mutex }; true)
  {
    auto& [is_cast_possible, map_source_static_types] = global_cache[destination_type][source_dynamic_type];
    if (destination_pointer != nullptr)
    {
      is_cast_possible = true;
      map_source_static_types[source_static_type] = offset;
    }
    else
    {
      is_cast_possible = false;
    }
    return destination_pointer;
  }
}

// cast from a reference type to a reference type
template<typename DestinationReference, typename SourceValue>
[[nodiscard]] inline std::enable_if_t<std::is_reference_v<DestinationReference>, DestinationReference>
cached_dynamic_cast(SourceValue& source_reference)
{
  static_assert(!std::is_pointer_v<SourceValue>); // casting from a reference to a pointer... what?
  static_assert(!std::is_rvalue_reference_v<DestinationReference>); // casting to an rvalue reference is not allowed

  using DestinationValue = std::remove_reference_t<DestinationReference>;
  static_assert(!std::is_pointer_v<DestinationValue>); // casting to a reference to a pointer... what??? :)

  DestinationValue* destination_pointer = cached_dynamic_cast<DestinationValue*>(std::addressof(source_reference));
  if (destination_pointer != nullptr)
    return *destination_pointer;
  else
    throw std::bad_cast{};
}

// cast from an lvalue `std::shared_ptr` to a `std::shared_ptr`
template<typename DestinationValue, typename SourceValue>
[[nodiscard]] inline std::shared_ptr<DestinationValue>
cached_dynamic_pointer_cast(const std::shared_ptr<SourceValue>& source_shared_pointer)
{
  auto* result = cached_dynamic_cast<typename std::shared_ptr<DestinationValue>::element_type*>(source_shared_pointer.get());
  if (result)
    return std::shared_ptr<DestinationValue>{source_shared_pointer, result};
  else
    return {};
}

// cast from an rvalue `std::shared_ptr` to a `std::shared_ptr`
template<typename DestinationValue, typename SourceValue>
[[nodiscard]] inline std::shared_ptr<DestinationValue>
cached_dynamic_pointer_cast(std::shared_ptr<SourceValue>&& source_shared_pointer)
{
  auto* result = cached_dynamic_cast<typename std::shared_ptr<DestinationValue>::element_type*>(source_shared_pointer.get());
  if (result)
    return std::shared_ptr<DestinationValue>{std::move(source_shared_pointer), result};
  else
    return {};
}
