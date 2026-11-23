#pragma once

#include <Common.h> // Windows.h with NOMINMAX, as the SDK headers need

#include <cstdint>
#include <string>

class ZObjectRef;

// A ZObjectRef's engine type name and, for int32, float32 and bool, its value,
// copied out of engine memory by SafeReadObjectValue.
struct ObjectValue {
    enum class Kind : uint8_t {
        Other, // no value read: another type, or no data
        Int32,
        Float32,
        Bool,
    };

    char typeName[48]; // empty when the object has no type
    Kind kind;
    uint32_t bits; // the value's bytes when kind is not Other
};

// Reads the type name and value of p_Object, guarded (see Guard.h). Returns 0
// or the structured-exception code of a fault.
DWORD SafeReadObjectValue(const ZObjectRef* p_Object, ObjectValue* p_Out);

// "<type name>" or "<type name>=<value>"; "<no type>" when it has none.
std::string FormatObjectValue(const ObjectValue& p_Value);
