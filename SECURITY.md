# Security policy

spc-cpp talks only to public, unauthenticated NOAA and IEM services, but it
often runs inside services that hold credentials. Please report
vulnerabilities in the library privately.

## Supported versions

Fixes land in the next release. Older tags are not patched, so upgrade your
`GIT_TAG` or `find_package` version to get them.

## Reporting a vulnerability

Use GitHub's [private vulnerability
reporting](https://github.com/Reddimus/spc-cpp/security/advisories/new). Do not
open a public issue.

Include the affected version or commit, a minimal reproduction, and the
impact you see, such as memory corruption or a denial of service.

You can expect an acknowledgement within 3 business days, an assessment
within 7, and then either a fixed release or a timeline for one.

## Out of scope

- Problems with the NOAA, SPC, or IEM services themselves. Report those to
  their operators.
- Rate limiting, network failures, or IEM outages. Open a regular issue.
- Vulnerabilities in libcurl, Glaze, or GoogleTest. Report them upstream; this
  project updates its pins when an advisory affects it.
