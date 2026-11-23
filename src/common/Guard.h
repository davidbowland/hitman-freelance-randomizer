#pragma once

#include <Common.h> // Windows.h with NOMINMAX, as the SDK headers need

#include <cstddef>
#include <cstdint>
#include <string>

class ZDynamicObject;
class ZEntitySceneContext;
class ZEntityType;
class ZObjectRef;

// Guarded engine reads. Raw-pointer steps come in pairs. The unguarded worker
// performs every dereference of the step (vtable loads, virtual calls, type
// lookups); the Safe* guard's __try body is exactly one call to it. clang-cl
// does not model asynchronous exceptions, so it catches a fault only when it
// is raised inside a called function, never by an instruction of the __try
// function itself. Workers are noinline and take and return only plain
// pointers and integers (or plain structs of them); guards keep no objects
// with destructors (so __try is allowed) and return 0 on success or the
// structured-exception code on a fault:
//
//     __declspec(noinline) void ThingOf(Engine* p_In, T* p_Out) { *p_Out = p_In->Thing(); }
//
//     __declspec(noinline) DWORD SafeGetThing(Engine* p_In, T* p_Out) {
//         DWORD s_Code = 0;
//         __try { ThingOf(p_In, p_Out); }
//         __except (CaptureCode(GetExceptionCode(), &s_Code)) {}
//         return s_Code;
//     }
//
// Every source file with guards is compiled with /EHa.

// The __except filter: handles only memory faults (access violation, in-page
// error, guard page), storing the code in *p_Out; any other exception continues to the
// next handler.
DWORD CaptureCode(DWORD p_Code, DWORD* p_Out);

// The engine's current scene context (Hitman5Module->m_pEntitySceneContext),
// read guarded; nullptr when there is none, or (reason logged) when the module
// is unavailable or the read faults.
ZEntitySceneContext* CurrentSceneContext();

// Shared guarded engine calls. Each returns 0, or the structured-exception
// code of a fault (outputs are then left as they were, unless the call says
// otherwise).

// Copies p_Size bytes from p_From to engine memory at p_Address (0xC0000005
// for a null address, as the write would fault).
uint32_t WriteEngineBytes(uintptr_t p_Address, const void* p_From, size_t p_Size);

// A pin's or event's payload: its type (STypeID*) and data pointers.
struct PinPayload {
    const void* type = nullptr;
    void* data = nullptr;
};

uint32_t ReadPinPayload(const ZObjectRef& p_Data, PinPayload* p_Out);

// The entity's logical parent (null when it has none; 0xC0000005 for a null
// entity, as the read would fault).
uint32_t ReadLogicalParent(ZEntityType** p_Entity, ZEntityType*** p_Out);

// Signals the entity's input pin p_Pin (its id, PinIds.h) with no payload.
uint32_t SendInputPin(ZEntityType** p_Entity, uint32_t p_Pin);

// The object as JSON (ZDynamicObject_ToString); *p_Json cleared on a fault.
uint32_t ReadDynamicObjectJson(const ZDynamicObject& p_Object, std::string* p_Json);
