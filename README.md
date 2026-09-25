# spc-cpp

[![CI](https://github.com/Reddimus/spc-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Reddimus/spc-cpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Reddimus/spc-cpp)](https://github.com/Reddimus/spc-cpp/releases)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A C++23 client for NOAA Storm Prediction Center products: convective and
fire-weather outlooks, mesoscale discussions, watches, and storm reports. It
reads NOAA's ArcGIS services, SPC's static GeoJSON feeds, and the Iowa
Environmental Mesonet archive. None of them need an API key.

```cpp
#include "spc/spc.hpp"

#include <iostream>

int main() {
    const spc::ArcGISClient client;
    const spc::Result<spc::CategoricalOutlookPayload> outlook = client.query_categorical(1);
    if (!outlook) {
        std::cerr << outlook.error().message << "\n";
        return 1;
    }
    for (const spc::OutlookFeature& band : outlook->features) {
        std::cout << band.label << " covers Wichita: "
                  << spc::point_in_feature(-97.34, 37.69, band) << "\n";
    }
}
```

## Requirements

- CMake 3.31 or newer, which Glaze requires. Ubuntu 24.04 ships 3.28, so
  install a newer one with `pipx install cmake` or from Kitware's APT
  repository.
- GCC 13+ or Clang 18+. CI also builds with the current Apple Clang.
- libcurl 7.85 or newer

CMake downloads Glaze and GoogleTest during configuration. Consumers of an
installed package need neither.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The tests never touch the network.

## Add it to your project

With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(spc_cpp
    GIT_REPOSITORY https://github.com/Reddimus/spc-cpp.git
    GIT_TAG v0.3.0
)
FetchContent_MakeAvailable(spc_cpp)
target_link_libraries(myapp PRIVATE spc::spc)
```

As a subproject, spc-cpp skips its tests, examples, and install rules. If you
install a library that links `spc::spc`, set `SPC_INSTALL` to `ON` before
`FetchContent_MakeAvailable` so spc-cpp's targets join an export set.

Or install it and use `find_package`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix /your/prefix
```

```cmake
find_package(spc 0.3 REQUIRED)
target_link_libraries(myapp PRIVATE spc::spc)
```

Until 1.0, a minor release can break the API, so `find_package` only accepts
the same minor version.

## Clients

| Client | Source | Products |
| --- | --- | --- |
| `ArcGISClient` | NOAA ArcGIS MapServers | Categorical, probabilistic, conditional intensity, days 4-8, fire weather, mesoscale discussions, raw layer queries |
| `StaticFeedClient` | SPC's GeoJSON files | Categorical, probabilistic, days 4-8 |
| `ArchiveClient` | Iowa Environmental Mesonet | Watches, Local Storm Reports |

`ArcGISClient` is the one to start with. It covers the most products and pages
through large results for you. `StaticFeedClient` is a fallback that reads the
same outlooks from SPC's own files. For watch boxes and storm reports, use
`ArchiveClient`, because NOAA's ArcGIS watch layer lacks SPC's watch details
and SPC publishes no storm-report layer.

Each client checks the day and hazard before sending anything, so a request
for day 4 categorical fails at once with `InvalidRequest`.

## Handling errors

Every call returns `spc::Result<T>`, an alias for `std::expected<T,
spc::Error>`. Nothing throws for network or data problems.
`Error::code` says what went wrong:

| Code | Meaning |
| --- | --- |
| `FeedUnavailable` | SPC has not issued this product right now. Treat it as no data. |
| `NotFound` | A wrong or retired URL or layer. Treat it as a bug to report. |
| `InvalidRequest` | An unsupported day or hazard, or a request ArcGIS rejected. |
| `RateLimited` | HTTP 429 or 503 after retries, or IEM's local rate limit. |
| `NetworkError`, `ServerError`, `ParseError` | Transport failure, HTTP 5xx, or unexpected JSON. |

Only `StaticFeedClient` returns `FeedUnavailable`. Overnight, for example,
there is often no day 1 probabilistic file. On ArcGIS the same situation is a
successful result with no features.

The standalone `parse_*` functions are the exception. They throw
`std::runtime_error` when given malformed JSON.

## Threads and networking

`HttpClient`, the default transport, is safe to share between threads. It
keeps a small pool of libcurl handles, so connections stay open between
requests. Clients built on it are safe to share too.

To use your own network stack, or canned responses in tests, implement
`spc::HttpTransport` and pass it in:

```cpp
std::shared_ptr<spc::HttpTransport> transport = std::make_shared<MyTransport>();
const spc::ArcGISClient client{transport};
```

`ArchiveClient` limits itself to a burst of 2 requests, then 1 per second,
because IEM is a free service run by Iowa State University.

## Examples

| Example | Shows |
| --- | --- |
| [`parse_outlook.cpp`](examples/parse_outlook.cpp) | Parsing a body offline and testing a point |
| [`arcgis.cpp`](examples/arcgis.cpp) | Categorical and tornado outlooks, active mesoscale discussions |
| [`fire_weather.cpp`](examples/fire_weather.cpp) | Fire weather for days 1 to 3 |
| [`static_feed.cpp`](examples/static_feed.cpp) | The static feed and `FeedUnavailable` |
| [`watches.cpp`](examples/watches.cpp) | Watches now and on a past date |
| [`archive.cpp`](examples/archive.cpp) | One NWS office's storm reports |

Run one with `make run-arcgis`, `make run-watches`, and so on.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Report security issues as described in
[SECURITY.md](SECURITY.md).

## Data sources

- [SPC products](https://www.spc.noaa.gov/products/)
- [SPC outlook MapServer](https://mapservices.weather.noaa.gov/vector/rest/services/outlooks/SPC_wx_outlks/MapServer)
- [SPC fire-weather MapServer](https://mapservices.weather.noaa.gov/vector/rest/services/fire_weather/SPC_firewx/MapServer)
- [SPC mesoscale discussion MapServer](https://mapservices.weather.noaa.gov/vector/rest/services/outlooks/spc_mesoscale_discussion/MapServer)
- [IEM JSON and GeoJSON services](https://mesonet.agron.iastate.edu/api/)

## License

[MIT](LICENSE)
