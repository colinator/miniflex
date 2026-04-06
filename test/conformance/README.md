# Conformance Tests

This directory contains the opt-in FlexBuffers conformance harness.

It validates `miniflex` against the official Python `flatbuffers.flexbuffers` implementation in both directions:

1. official FlexBuffers output can be parsed by `miniflex`
2. `miniflex` output can be parsed by official FlexBuffers

## Prerequisites

You need the official Python `flatbuffers` package available to the interpreter used for the conformance build.

For example:

```sh
python3 -m pip install flatbuffers
```

## Configure And Build

Run these commands from the repository root.

```sh
cmake -S . -B build-conformance -DMINIFLEX_BUILD_CONFORMANCE=ON
cmake --build build-conformance
```

## Run The Tests

Also run these from the repository root.

Via CTest:

```sh
ctest --test-dir build-conformance --output-on-failure
```

Or directly:

```sh
python3 test/conformance/run_conformance.py ./build-conformance/miniflex_conformance_cases
```

## Notes

`build-conformance/` is just a generated build directory. You can delete it at any time and recreate it with the configure command above.
