// Minimal stand-in for Boost.Config's <boost/cstdint.hpp>, providing only the
// fixed-width integer aliases used by the vendored Boost.Polygon Voronoi headers.
#ifndef BOOST_CSTDINT_HPP
#define BOOST_CSTDINT_HPP
#include <cstdint>
namespace boost
{
using std::int8_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
} // namespace boost
#endif
