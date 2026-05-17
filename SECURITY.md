# Security Policy

`spc-cpp` is a third-party C++ client for NOAA Storm Prediction Center
(SPC) public products (static GeoJSON feeds, the ArcGIS MapServer, and
the IEM historical archive). Those endpoints are unauthenticated, but
consumers of this library may run it inside services that handle
credentials or sensitive operator data. This file is the canonical
contact path for reporting a vulnerability in the client.

## Supported Versions

Security fixes are made on the latest published `vX.Y.Z` tag. Older
tags are not back-patched — bump your `FetchContent_Declare(... GIT_TAG ...)`
pin or your `find_package(spc X.Y.Z REQUIRED)` constraint to the
latest minor on the same major as part of the upgrade.

| Version    | Supported          |
| ---------- | ------------------ |
| latest tag | :white_check_mark: |
| older      | :x:                |

## Reporting a Vulnerability

**Do not open a public issue.** Use GitHub's [private vulnerability
reporting](https://github.com/Reddimus/spc-cpp/security/advisories/new)
flow, which delivers the report to the maintainer privately and tracks
coordinated disclosure.

When reporting, please include:

- Affected version (tag or commit SHA)
- A reproduction — minimal code or test case
- Impact (memory corruption / DoS / something else)
- Whether you've notified anyone else

You can expect:

- Acknowledgement within **3 business days**
- An initial assessment + severity rating within **7 business days**
- A fix on a new `vX.Y.Z+1` tag, or a clear timeline if the fix is
  larger

## Out of Scope

- Bugs against the SPC / NWS MapServer / IEM endpoints themselves —
  those go to the respective operators, not this client library.
- Operational issues (rate-limit handling, network blips, third-party
  IEM flakiness) — file a regular issue.
- Theoretical issues against dependencies — report them upstream
  (`libcurl`, `glaze`, `googletest`). We pin via FetchContent and bump
  on credible advisories.
