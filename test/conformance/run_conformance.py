#!/usr/bin/env python3.11
import os
import subprocess
import sys
import tempfile

from flatbuffers import flexbuffers


CASES = [
    "scalar_int",
    "scalar_null",
    "scalar_uint",
    "scalar_float",
    "scalar_bool",
    "scalar_string",
    "scalar_blob",
    "vector_mixed",
    "typed_vector_int",
    "typed_vector_uint",
    "typed_vector_float",
    "fixed_vector_int2",
    "fixed_vector_float3",
    "fixed_vector_uint4",
    "vector_bool",
    "vector_key",
    "vector_string_deprecated",
    "map_nested",
    "indirect_scalars_map",
]


def expected_value(name):
    if name == "scalar_int":
        return -12345
    if name == "scalar_null":
        return None
    if name == "scalar_uint":
        return 1234567890123
    if name == "scalar_float":
        return 3.25
    if name == "scalar_bool":
        return True
    if name == "scalar_string":
        return "hello"
    if name == "scalar_blob":
        return bytes([1, 2, 3, 4])
    if name == "vector_mixed":
        return [7, "two", True]
    if name == "typed_vector_int":
        return [-1, 2, 300]
    if name == "typed_vector_uint":
        return [1, 2, 4000000000]
    if name == "typed_vector_float":
        return [1.25, 2.5, 8.75]
    if name == "fixed_vector_int2":
        return [-7, 11]
    if name == "fixed_vector_float3":
        return [1.5, 2.5, 3.5]
    if name == "fixed_vector_uint4":
        return [4, 5, 6, 7]
    if name == "vector_bool":
        return [True, False, True]
    if name == "vector_key":
        return ["a", "b"]
    if name == "vector_string_deprecated":
        return ["alpha", "beta"]
    if name == "map_nested":
        return {
            "count": 2,
            "items": [
                {"name": "first", "ok": True},
                {"name": "second", "ok": False},
            ],
        }
    if name == "indirect_scalars_map":
        return {"i": -77, "u": 99999, "f": 6.5}
    raise KeyError(name)


def build_official_case(name):
    b = flexbuffers.Builder()
    if name == "scalar_int":
        b.Int(-12345)
    elif name == "scalar_null":
        b.Null()
    elif name == "scalar_uint":
        b.UInt(1234567890123)
    elif name == "scalar_float":
        b.Float(3.25)
    elif name == "scalar_bool":
        b.Bool(True)
    elif name == "scalar_string":
        b.String("hello")
    elif name == "scalar_blob":
        b.Blob(bytes([1, 2, 3, 4]))
    elif name == "vector_mixed":
        with b.Vector():
            b.Int(7)
            b.String("two")
            b.Bool(True)
    elif name == "typed_vector_int":
        with b.TypedVector():
            b.Int(-1)
            b.Int(2)
            b.Int(300)
    elif name == "typed_vector_uint":
        with b.TypedVector():
            b.UInt(1)
            b.UInt(2)
            b.UInt(4000000000)
    elif name == "typed_vector_float":
        with b.TypedVector():
            b.Float(1.25)
            b.Float(2.5)
            b.Float(8.75)
    elif name == "fixed_vector_int2":
        b.FixedTypedVectorFromElements([-7, 11])
    elif name == "fixed_vector_float3":
        b.FixedTypedVectorFromElements([1.5, 2.5, 3.5])
    elif name == "fixed_vector_uint4":
        b.FixedTypedVectorFromElements([4, 5, 6, 7], element_type=flexbuffers.Type.UINT)
    elif name == "vector_bool":
        with b.TypedVector():
            b.Bool(True)
            b.Bool(False)
            b.Bool(True)
    elif name == "vector_key":
        with b.TypedVector():
            b.Key("a")
            b.Key("b")
    elif name == "vector_string_deprecated":
        with b.TypedVector():
            b.String("alpha")
            b.String("beta")
    elif name == "map_nested":
        with b.Map():
            b.UInt("count", 2)
            with b.Vector("items"):
                with b.Map():
                    b.String("name", "first")
                    b.Bool("ok", True)
                with b.Map():
                    b.String("name", "second")
                    b.Bool("ok", False)
    elif name == "indirect_scalars_map":
        with b.Map():
            b.IndirectInt("i", -77)
            b.IndirectUInt("u", 99999)
            b.IndirectFloat("f", 6.5)
    else:
        raise KeyError(name)
    return bytes(b.Finish())


def normalize(value):
    if isinstance(value, bytearray):
        return bytes(value)
    if isinstance(value, bytes):
        return value
    if isinstance(value, list):
        return [normalize(v) for v in value]
    if isinstance(value, dict):
        return {k: normalize(v) for k, v in value.items()}
    return value


def compare(actual, expected):
    actual = normalize(actual)
    expected = normalize(expected)
    if isinstance(expected, float):
        return abs(actual - expected) < 1e-6
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(compare(a, e) for a, e in zip(actual, expected))
    if isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(compare(actual[k], expected[k]) for k in expected)
    return actual == expected


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/miniflex_cases", file=sys.stderr)
        return 2

    tool = sys.argv[1]
    with tempfile.TemporaryDirectory() as td:
        for name in CASES:
            official = build_official_case(name)
            official_path = os.path.join(td, f"{name}.official.flex")
            emitted_path = os.path.join(td, f"{name}.miniflex.flex")

            with open(official_path, "wb") as f:
                f.write(official)

            subprocess.run([tool, "check", name, official_path], check=True)
            subprocess.run([tool, "emit", name, emitted_path], check=True)

            with open(emitted_path, "rb") as f:
                emitted = f.read()

            actual = flexbuffers.GetRoot(emitted).Value
            expected = expected_value(name)
            if not compare(actual, expected):
                raise AssertionError(f"{name}: expected {expected!r}, got {actual!r}")

    print(f"{len(CASES)} conformance cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
