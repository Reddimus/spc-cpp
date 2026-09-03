# spc-cpp

[![CI](https://github.com/Reddimus/spc-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/Reddimus/spc-cpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Reddimus/spc-cpp)](https://github.com/Reddimus/spc-cpp/releases)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

spc-cpp is a C++23 SDK for NOAA Storm Prediction Center weather products. It
reads SPC's ArcGIS services, static GeoJSON feeds, and selected Iowa
Environmental Mesonet archives. Every fallible SDK operation returns
`std::expected`; no API key is required.

## First run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The build requires CMake, a C++23 compiler, and libcurl.

## Use the SDK

```cpp
#include "spc/spc.hpp"

spc::ArcGISClient client;
spc::Result<spc::ProbOutlookPayload> outlook =
    client.query_probabilistic(1, "tornado");
if (!outlook) {
    // Inspect outlook.error().code and outlook.error().message.
}
```

The clients validate day and product combinations before making a request.
Supported ArcGIS products include:

- Day 1 through 3 categorical outlooks.
- Day 1 and 2 tornado, hail, and wind probabilities.
- Day 3 severe probability.
- Day 1 through 3 conditional intensity.
- Day 4 through 8 severe probability.
- Day 1 through 8 fire weather, merging both published layers for each day.
  Days 1 and 2 report categorical severity; days 3 through 8 report normalized
  probability.
- Active mesoscale discussions.

`StaticFeedClient` provides categorical, probabilistic, and day 4 through 8
fallbacks. `ArchiveClient` reads IEM watch and storm-report data.

### Active watches

Use `ArchiveClient::watches()` for SPC watch boxes. NOAA's WWA ArcGIS service
publishes CAP and WFO polygons, but it omits the SPC parameters represented by
`WatchPayload`, including PDS status and maximum hail and wind. The old
`ArcGISClient::query_active_watches()` method is deprecated and returns
`InvalidRequest` instead of fabricating incomplete watches.

### Custom networking

`HttpClient` is the default GET transport. Implement `HttpTransport` and pass
a `std::shared_ptr<HttpTransport>` to a high-level client to use another
network stack or deterministic test responses.

## Install

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSPC_BUILD_TESTS=OFF
cmake --build build --parallel
cmake --install build --prefix /your/prefix
```

```cmake
find_package(spc 0.2 REQUIRED)
target_link_libraries(myapp PRIVATE spc::spc)
```

FetchContent consumers can pin the release:

```cmake
include(FetchContent)
FetchContent_Declare(spc_cpp
    GIT_REPOSITORY https://github.com/Reddimus/spc-cpp.git
    GIT_TAG v0.2.0
)
FetchContent_MakeAvailable(spc_cpp)
target_link_libraries(myapp PRIVATE spc::spc)
```

## Project map

| Path | Contents |
| --- | --- |
| `include/spc/` | Public clients, models, errors, and geometry helpers |
| `src/api/` | Static feed, ArcGIS, and IEM routing |
| `src/models/` | Glaze-backed GeoJSON and Esri parsers |
| `src/http/` | libcurl transport |
| `tests/` | Public client tests and captured parser fixtures |
| `tools/` | Style, consumer, and live metadata checks |

The static libraries form this dependency chain:
`spc_core -> spc_http -> spc_models -> spc_api`. Link `spc::spc` unless you
need one layer directly.

## Verify changes

```bash
make test
make lint
make test-consumers
make fixtures-check
python3 tools/verify_arcgis_metadata.py  # requires network access
```

The normal unit suite does not depend on NOAA availability. The metadata
command compares the live ArcGIS layer IDs and names with the checked
2026-09-03 contract, then queries all 39 feature layers.

SPC payloads vary in key case, numeric representation, and geometry type. The
parsers use Glaze 8.3's generic JSON tree to handle those shapes. The original
convective parser stays aligned with the downstream `spc-data` service; new
product parsers keep their own label mappings.

## References

- [SPC products](https://www.spc.noaa.gov/products/)
- [SPC outlook MapServer](https://mapservices.weather.noaa.gov/vector/rest/services/outlooks/SPC_wx_outlks/MapServer)
- [SPC fire-weather MapServer](https://mapservices.weather.noaa.gov/vector/rest/services/fire_weather/SPC_firewx/MapServer)
- [IEM SPC archive](https://mesonet.agron.iastate.edu/)

## License

[MIT](LICENSE)
