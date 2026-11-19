# Boost.Polygon Voronoi subset

Seven headers from Boost.Polygon (tag `boost-1.88.0`, https://github.com/boostorg/polygon),
unmodified, under the Boost Software License 1.0 (`LICENSE_1_0.txt`):

- `boost/polygon/voronoi_builder.hpp`, `voronoi_diagram.hpp`, `voronoi_geometry_type.hpp`
- `boost/polygon/detail/voronoi_ctypes.hpp`, `voronoi_predicates.hpp`, `voronoi_robust_fpt.hpp`, `voronoi_structures.hpp`

`boost/cstdint.hpp` is a local shim that maps the fixed-width integer types to `<cstdint>`,
so no other part of Boost is needed. `boost/polygon/voronoi.hpp` is intentionally omitted
(it pulls in the rest of Boost.Polygon); use `voronoi_builder` directly.
