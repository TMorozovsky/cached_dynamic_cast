#include "cached_dynamic_cast.hpp"

namespace detail::cached_dynamic_cast_detail
{
  global_cache_type global_cache{};
  std::shared_mutex global_cache_mutex{};
}
