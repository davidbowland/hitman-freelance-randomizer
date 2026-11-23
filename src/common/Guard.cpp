#include "Guard.h"

#include "ModLog.h"

#include <Functions.h>
#include <Globals.h>

#include <Glacier/ZEntity.h>
#include <Glacier/ZModule.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZString.h>

#include <cstdint>
#include <cstring>

namespace {

__declspec(noinline) void SceneContextOf(ZHitman5Module* p_Module, ZEntitySceneContext** p_Out) {
    *p_Out = p_Module->m_pEntitySceneContext;
}

__declspec(noinline) DWORD SafeGetSceneContext(ZHitman5Module* p_Module, ZEntitySceneContext** p_Out) {
    DWORD s_Code = 0;

    __try {
        SceneContextOf(p_Module, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void CopyToEngine(uintptr_t p_Address, const void* p_From, size_t p_Size) {
    std::memcpy(reinterpret_cast<void*>(p_Address), p_From, p_Size);
}

__declspec(noinline) DWORD SafeCopyToEngine(uintptr_t p_Address, const void* p_From, size_t p_Size) {
    DWORD s_Code = 0;

    __try {
        CopyToEngine(p_Address, p_From, p_Size);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void PayloadOf(const ZObjectRef* p_Data, PinPayload* p_Out) {
    p_Out->type = p_Data->GetTypeID();
    p_Out->data = p_Data->GetData();
}

__declspec(noinline) DWORD SafePayload(const ZObjectRef* p_Data, PinPayload* p_Out) {
    DWORD s_Code = 0;

    __try {
        PayloadOf(p_Data, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void LogicalParentOf(ZEntityType** p_Entity, ZEntityType*** p_Out) {
    *p_Out = ZEntityRef(p_Entity).GetLogicalParent().m_pObj;
}

__declspec(noinline) DWORD SafeLogicalParent(ZEntityType** p_Entity, ZEntityType*** p_Out) {
    DWORD s_Code = 0;

    __try {
        LogicalParentOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void SignalPinOf(ZEntityType** p_Entity, uint32_t p_Pin) {
    ZEntityRef(p_Entity).SignalInputPin(p_Pin);
}

__declspec(noinline) DWORD SafeSignalPin(ZEntityType** p_Entity, uint32_t p_Pin) {
    DWORD s_Code = 0;

    __try {
        SignalPinOf(p_Entity, p_Pin);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// ZDynamicObject_ToString takes a non-const pointer and only reads it. The
// ZString (it has a destructor) belongs to the caller.
__declspec(noinline) void JsonOf(const ZDynamicObject* p_Object, ZString* p_Out) {
    Functions::ZDynamicObject_ToString->Call(const_cast<ZDynamicObject*>(p_Object), *p_Out);
}

__declspec(noinline) DWORD SafeJson(const ZDynamicObject* p_Object, ZString* p_Out) {
    DWORD s_Code = 0;

    __try {
        JsonOf(p_Object, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

DWORD CaptureCode(DWORD p_Code, DWORD* p_Out) {
    // A guard page (PAGE_GUARD) raises STATUS_GUARD_PAGE_VIOLATION once and the
    // system clears the guard as it does; a raw read must not crash on it.
    // (Stack growth guard pages are handled by the kernel and never get here.)
    if (p_Code != EXCEPTION_ACCESS_VIOLATION && p_Code != EXCEPTION_IN_PAGE_ERROR && p_Code != EXCEPTION_GUARD_PAGE)
        return EXCEPTION_CONTINUE_SEARCH;

    *p_Out = p_Code;
    return EXCEPTION_EXECUTE_HANDLER;
}

ZEntitySceneContext* CurrentSceneContext() {
    if (!Globals::Hitman5Module) {
        Log("scene context: Hitman5Module not available");
        return nullptr;
    }

    ZEntitySceneContext* s_Context = nullptr;

    if (const DWORD s_Code = SafeGetSceneContext(Globals::Hitman5Module, &s_Context)) {
        Log("scene context: fault reading it (exception 0x{:08X})", static_cast<uint32_t>(s_Code));
        return nullptr;
    }

    // No context (a menu, between levels) is normal, so it is not logged.
    return s_Context;
}

uint32_t WriteEngineBytes(uintptr_t p_Address, const void* p_From, size_t p_Size) {
    if (p_Address == 0)
        return EXCEPTION_ACCESS_VIOLATION;

    return static_cast<uint32_t>(SafeCopyToEngine(p_Address, p_From, p_Size));
}

uint32_t ReadPinPayload(const ZObjectRef& p_Data, PinPayload* p_Out) {
    PinPayload s_Out;

    if (const DWORD s_Code = SafePayload(&p_Data, &s_Out))
        return static_cast<uint32_t>(s_Code);

    *p_Out = s_Out;
    return 0;
}

uint32_t ReadLogicalParent(ZEntityType** p_Entity, ZEntityType*** p_Out) {
    if (!p_Entity)
        return EXCEPTION_ACCESS_VIOLATION;

    ZEntityType** s_Parent = nullptr;

    if (const DWORD s_Code = SafeLogicalParent(p_Entity, &s_Parent))
        return static_cast<uint32_t>(s_Code);

    *p_Out = s_Parent;
    return 0;
}

uint32_t SendInputPin(ZEntityType** p_Entity, uint32_t p_Pin) {
    if (!p_Entity)
        return EXCEPTION_ACCESS_VIOLATION;

    return static_cast<uint32_t>(SafeSignalPin(p_Entity, p_Pin));
}

uint32_t ReadDynamicObjectJson(const ZDynamicObject& p_Object, std::string* p_Json) {
    ZString s_Json;

    if (const DWORD s_Code = SafeJson(&p_Object, &s_Json)) {
        p_Json->clear();
        return static_cast<uint32_t>(s_Code);
    }

    *p_Json = std::string(s_Json.ToStringView());
    return 0;
}
