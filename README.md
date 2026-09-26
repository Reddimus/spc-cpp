# spc-cpp

[![CI](https://github.com/Reddimus/spc-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Reddimus/spc-cpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Reddimus/spc-cpp)](https://github.com/Reddimus/spc-cpp/releases)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A C++23 client for NOAA Storm Prediction Center data: convective and
fire-weather outlooks, mesoscale discussions, watches, and storm reports. It
reads [NOAA's ArcGIS services](https://mapservices.weather.noaa.gov/vector/rest/services/),
[SPC's GeoJSON files](https://www.spc.noaa.gov/gis/), and the
[Iowa Environmental Mesonet](https://mesonet.agron.iastate.edu/api/) archive.
None of them needs an API key.

```cpp
#include "spc/spc.hpp"

#include <iostream>

int main() {
    const spc::ArcGISClient client;
    const spc::Result<spc::CategoricalOutlookPayload> day1 = client.query_categorical(1);
    if (!day1) {
        std::cerr << day1.error().message << "\n";
        return 1;
    }
    std::cout << "Day 1 bands: " << day1->features.size() << "\n";
    for (const spc::OutlookFeature& band : day1->features) {
        if (spc::point_in_feature(-97.34, 37.69, band)) { // lon, lat of Wichita
            std::cout << "Wichita is in " << band.label << "\n";
        }
    }
}
```

## Add it to your project

- CMake 3.31 or newer. Ubuntu 24.04 ships 3.28, so run
  `sudo snap install cmake --classic`.
- GCC 13+, Clang 18+ with libc++ or 19+ with libstdc++, or Apple Clang 17+.
- libcurl 7.85 or newer with its headers: `libcurl4-openssl-dev` on Ubuntu,
  built into macOS.

CI builds on Linux, and on macOS for Apple silicon and Intel. Windows is
untested.

Save the example above as `main.cpp` and this as `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(myapp LANGUAGES CXX)
include(FetchContent)
FetchContent_Declare(spc_cpp
    GIT_REPOSITORY https://github.com/Reddimus/spc-cpp.git
    GIT_TAG v0.4.4
)
FetchContent_MakeAvailable(spc_cpp)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE spc::spc)
```

Build and run it:

```bash
cmake -S . -B build
cmake --build build --parallel
./build/myapp
```

Day 1 is today's outlook. The program prints lines like `Wichita is in SLGT`,
SPC's code for a slight risk. On a quiet day, `Day 1 bands: 0` is a normal
answer, not an error.

On a Mac the build is native to your chip. To run on older Macs, add
`-DCMAKE_OSX_DEPLOYMENT_TARGET=13.4` or later. For one binary that also runs
on Intel, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; it needs the macOS
libcurl, because Homebrew's curl is arm64 only.

If your own installed library links `spc::spc`, add `set(SPC_INSTALL ON)`
before `FetchContent_MakeAvailable`. Otherwise your `install(EXPORT)` fails.

Or, from a clone of spc-cpp, install it and use `find_package`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSPC_BUILD_TESTS=OFF
cmake --build build --parallel
cmake --install build --prefix /your/prefix
```

```cmake
find_package(spc 0.4 REQUIRED)
target_link_libraries(myapp PRIVATE spc::spc)
```

Configure your project with `-DCMAKE_PREFIX_PATH=/your/prefix`. Before 1.0,
a minor release can break the API, so `find_package(spc 0.4)` accepts only
0.4.x.

## Which call to use

| Product | Call |
| --- | --- |
| Categorical outlook, days 1-3 | `ArcGISClient::query_categorical(day)` |
| Probabilistic outlook, days 1-3 | `ArcGISClient::query_probabilistic(day, hazard)` |
| Conditional intensity, days 1-3 | `ArcGISClient::query_conditional_intensity(day, hazard)` |
| Severe outlook, days 4-8 | `ArcGISClient::query_day4_8(day)` |
| Fire weather, days 1-8 | `ArcGISClient::query_fire_weather(day)` |
| Active mesoscale discussions | `ArcGISClient::query_active_md()` |
| Any other layer on those ArcGIS services, as raw JSON | `ArcGISClient::query_layer(service, layer_id, params)` |
| Watches, now or at a past time | `ArchiveClient::watches(timestamp)` |
| Storm reports between two times | `ArchiveClient::storm_reports(start, end, office)` |

`hazard` is `"tornado"`, `"hail"`, or `"wind"` on days 1 and 2, and
`"severe"` on day 3. [`include/spc/api.hpp`](include/spc/api.hpp) documents
every call's arguments. As a fallback, `StaticFeedClient::day_categorical`,
`day_probabilistic`, and `day4_8` read the same outlooks from SPC's GeoJSON
files.

The Iowa Environmental Mesonet is a free service, so each `ArchiveClient`
allows a burst of 2 requests, then 1 per second, retries included. Share one
instance so the limit holds across your program.

## Handling errors

Client methods return `spc::Result<T>`, an alias for
`std::expected<T, spc::Error>`. They do not throw for network or data
problems. `Error::code` says what went wrong:

| Code | Meaning |
| --- | --- |
| `NetworkError` | Connection, TLS, or timeout failure, after retries. |
| `RateLimited` | HTTP 429 or 503 after retries, or `ArchiveClient`'s own limit would wait more than 5 s. |
| `ServerError` | HTTP 5xx other than 503, after retries, or an ArcGIS query that never finished paging. |
| `InvalidRequest` | An unsupported day or hazard, HTTP 400, an ArcGIS error such as a bad layer ID, or a response over 64 MiB. |
| `NotFound` | A URL or ArcGIS service that no longer exists. Report it as a bug. |
| `ParseError` | The body was not valid JSON. |
| `FeedUnavailable` | Only `StaticFeedClient` returns it. SPC has not issued the product, as with day 1 probabilities overnight. Treat it as no data. `ArcGISClient` returns an empty result instead. |
| `Unknown` | Any other HTTP status, such as 422 for a malformed `ArchiveClient` timestamp. `Error::http_status` holds it. |

The offline `parse_*` functions, such as `spc::parse_categorical(body, day)`,
throw `std::runtime_error` on malformed JSON.

## Settings and threads

`spc::ClientConfig` sets the user agent, the 15 s timeout, and the 64 MiB
response limit. The default user agent carries spc-cpp's contact address, so
set one with yours:

```cpp
const spc::ArcGISClient client{spc::ClientConfig{.user_agent = "myapp/1.0 (you@example.com)"}};
```

Calls block until they finish, retry and rate-limit waits included. Clients
that use the default `HttpClient` are safe to share between threads. To swap
the network stack or return canned test responses, subclass
`spc::HttpTransport`. Its one method, `get`, receives a full URL and must be
thread-safe:

```cpp
struct MyTransport final : spc::HttpTransport {
    spc::Result<spc::HttpResponse> get(std::string_view url) const override;
};
const spc::ArcGISClient client{std::make_shared<MyTransport>()};
```

## Examples

From a clone, `make run-<name>` builds and runs an [example](examples/), such
as `make run-fire_weather`. `parse_outlook` needs no network.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Report security issues through
[SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE)
