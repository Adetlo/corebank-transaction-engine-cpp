# Security Policy

## Scope

CoreBank Transaction Engine C++ is an **educational reference implementation**. It must
never process real financial transactions, real customer data or real account
identifiers. Every identifier in this repository, its tests, examples and demo scripts
is synthetic.

Authentication, authorisation and transport encryption are intentionally **out of
scope for v1**: the TCP listener binds to loopback by default and is meant to sit
behind a trusted gateway. See [`docs/SECURITY.md`](docs/SECURITY.md) for the threat
model, the controls that *are* implemented (input validation, bounded payloads,
parameterised SQL, immutability triggers, non-root container, restrictive file
permissions) and what a production deployment would add.

## Supported versions

Only the `main` branch receives fixes.

## Reporting a vulnerability

If you find a security issue in this project:

1. Please do **not** open a public issue with exploit details.
2. Use GitHub's private vulnerability reporting on the repository ("Security" tab,
   "Report a vulnerability") so the report stays confidential.
3. Include the affected component, a minimal reproduction and the impact you believe
   it has.

You can expect an acknowledgement within a reasonable time frame for a personal
open-source project, and credit in the fix if you want it.
