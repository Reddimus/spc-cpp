/// @file geo.hpp
/// @brief GeoJSON + Esri-FeatureSet wrapper shapes.
///
/// The verbatim convective parser walks the raw `glz::generic` AST (it has
/// to — SPC's properties block is shape-loose). These typed wrappers are for
/// net-new models and the ArcGIS path; `parse_esri_rings` (the Esri-rings
/// adapter, kept SEPARATE from the verbatim `detail::parse_rings` to preserve
/// GeoJSON parity) is added in PR-2.

#pragma once

#include "spc/types.hpp"

#include <string>
#include <vector>

namespace spc {

/// A generic GeoJSON Feature: typed properties + already-flattened rings
/// (via `detail::parse_rings`, so Polygon/MultiPolygon are uniform).
template <typename Properties>
struct GeoJsonFeature {
	std::string type{"Feature"};
	Properties properties;
	std::vector<Polygon> rings;
};

/// A GeoJSON FeatureCollection.
template <typename Properties>
struct GeoJsonFeatureCollection {
	std::string type{"FeatureCollection"};
	std::vector<GeoJsonFeature<Properties>> features;
};

/// An ArcGIS Esri `FeatureSet` (the `f=json` response shape): `attributes`
/// map + `geometry.rings`. Populated by the ArcGIS path in PR-2.
template <typename Attributes>
struct EsriFeature {
	Attributes attributes;
	std::vector<Polygon> rings;
};

template <typename Attributes>
struct EsriFeatureSet {
	std::string geometry_type;
	bool exceeded_transfer_limit{false};
	std::vector<EsriFeature<Attributes>> features;
};

} // namespace spc
