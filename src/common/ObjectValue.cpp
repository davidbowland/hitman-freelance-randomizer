#include "ObjectValue.h"

#include "Guard.h"
#include "ModLog.h"

#include <Glacier/ZEntity.h>

#include <Glacier/CompileReflection.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string_view>

namespace {

void CopyBytes(const char* p_Text, size_t p_Length, char* p_Out, size_t p_Capacity) {
    const size_t s_Length = p_Text ? std::min(p_Length, p_Capacity - 1) : 0;

    if (s_Length > 0)
        std::memcpy(p_Out, p_Text, s_Length);

    p_Out[s_Length] = '\0';
}

// Workers and guards follow the pattern in Guard.h.

__declspec(noinline) void ObjectValueOf(const ZObjectRef* p_Object, ObjectValue* p_Out) {
    *p_Out = {};
    const STypeID* s_TypeId = p_Object->GetTypeID();
    const IType* s_TypeInfo = s_TypeId ? s_TypeId->GetTypeInfo() : nullptr;
    const char* s_Name = s_TypeInfo ? s_TypeInfo->pszTypeName : nullptr;

    if (!s_Name)
        return;

    CopyBytes(s_Name, std::strlen(s_Name), p_Out->typeName, sizeof(p_Out->typeName));
    const void* s_Data = p_Object->GetData();
    const std::string_view s_Type = p_Out->typeName;

    if (!s_Data)
        return;

    if (s_Type == ZHMTypeName<int32_t>) {
        p_Out->kind = ObjectValue::Kind::Int32;
        std::memcpy(&p_Out->bits, s_Data, sizeof(int32_t));
    }
    else if (s_Type == ZHMTypeName<float>) {
        p_Out->kind = ObjectValue::Kind::Float32;
        std::memcpy(&p_Out->bits, s_Data, sizeof(float));
    }
    else if (s_Type == ZHMTypeName<bool>) {
        p_Out->kind = ObjectValue::Kind::Bool;
        p_Out->bits = *static_cast<const uint8_t*>(s_Data);
    }
}

} // namespace

DWORD SafeReadObjectValue(const ZObjectRef* p_Object, ObjectValue* p_Out) {
    DWORD s_Code = 0;

    __try {
        ObjectValueOf(p_Object, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

std::string FormatObjectValue(const ObjectValue& p_Value) {
    if (p_Value.typeName[0] == '\0')
        return "<no type>";

    switch (p_Value.kind) {
        case ObjectValue::Kind::Int32:
            return fmt::format("{}={}", p_Value.typeName, std::bit_cast<int32_t>(p_Value.bits));
        case ObjectValue::Kind::Float32:
            return fmt::format("{}={:.9g}", p_Value.typeName, std::bit_cast<float>(p_Value.bits));
        case ObjectValue::Kind::Bool: return fmt::format("{}={}", p_Value.typeName, p_Value.bits != 0);
        case ObjectValue::Kind::Other: break;
    }

    return p_Value.typeName;
}
