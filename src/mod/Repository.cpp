#include "Repository.h"

#include "Guard.h"
#include "ModLog.h"
#include "RepoPass.h"

#include <Glacier/CompileReflection.h>
#include <Glacier/THashMap.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZPrimitives.h>
#include <Glacier/ZString.h>

#include <Globals.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

using RepoMap = THashMap<ZRepositoryID, ZDynamicObject, TDefaultHashMapPolicy<ZRepositoryID>>;
using EntryArray = TArray<SDynamicObjectKeyValuePair>;

constexpr auto kProRepo = ResId<"[assembly:/repository/pro.repo].pc_repo">;
static_assert(sizeof(ZRepositoryID) == sizeof(fr::Guid), "ZRepositoryID is copied byte for byte into fr::Guid");

void CopyText(const char* p_Text, size_t p_Length, char* p_Out, size_t p_OutSize) {
    const size_t s_Count = std::min(p_Length, p_OutSize - 1);
    std::memcpy(p_Out, p_Text, s_Count);
    p_Out[s_Count] = '\0';
}

// The engine type name of a dynamic object's value, or nullptr.
const char* TypeNameOf(const ZObjectRef& p_Object) {
    const STypeID* s_TypeId = p_Object.GetTypeID();
    const IType* s_TypeInfo = s_TypeId ? s_TypeId->GetTypeInfo() : nullptr;
    return s_TypeInfo ? s_TypeInfo->pszTypeName : nullptr;
}

// Raw-pointer steps follow the guard pattern in Guard.h.

struct ResourceState {
    int status;
    void* data;
};

__declspec(noinline) void ResourceStateOf(const ZResourcePtr* p_Resource, ResourceState* p_Out) {
    const auto& s_Info = p_Resource->GetResourceInfo();
    *p_Out = {static_cast<int>(s_Info.status), s_Info.resourceData};
}

__declspec(noinline) DWORD SafeResourceState(const ZResourcePtr* p_Resource, ResourceState* p_Out) {
    DWORD s_Code = 0;

    __try {
        ResourceStateOf(p_Resource, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void EntryCountOf(const void* p_Map, size_t* p_Out) {
    *p_Out = static_cast<const RepoMap*>(p_Map)->size();
}

__declspec(noinline) DWORD SafeEntryCount(const void* p_Map, size_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        EntryCountOf(p_Map, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct SortedKey {
    fr::Guid guid;
    size_t index;
};

struct LookupArgs {
    void* map;
    const SortedKey* keys;
    size_t count;
    RepoLookup* out;
    size_t entryCount; // the map's size before the pass
    size_t scanned;
    bool overran; // more nodes than entryCount + 1: stopped (broken links)
};

void FillLookup(const ZDynamicObject& p_Entry, RepoLookup* p_Out) {
    p_Out->entryFound = true;
    p_Out->entry = &p_Entry;

    const EntryArray* s_Pairs = p_Entry.As<EntryArray>();

    if (!s_Pairs)
        return;

    p_Out->entryIsObject = true;

    for (size_t i = 0; i < s_Pairs->size(); ++i) {
        const SDynamicObjectKeyValuePair& s_Pair = (*s_Pairs)[i];
        const std::string_view s_Key = s_Pair.sKey.ToStringView();

        if (s_Key == "Toggle") {
            if (bool* s_Bool = s_Pair.value.As<bool>()) {
                p_Out->hasToggle = true;
                p_Out->toggle = *s_Bool;
                p_Out->toggleData = s_Bool;
            }

            continue;
        }

        if (s_Key != "Value" || p_Out->hasValue)
            continue;

        p_Out->hasValue = true;

        if (const char* s_Name = TypeNameOf(s_Pair.value))
            CopyText(s_Name, std::strlen(s_Name), p_Out->typeName, sizeof(p_Out->typeName));

        if (float* s_Float = s_Pair.value.As<float>()) {
            p_Out->kind = RepoValueKind::Float32;
            p_Out->value = *s_Float;
            p_Out->valueData = s_Float;
        }
        else if (double* s_Double = s_Pair.value.As<double>()) {
            p_Out->kind = RepoValueKind::Float64;
            p_Out->value = *s_Double;
            p_Out->valueData = s_Double;
        }
        else {
            p_Out->kind = RepoValueKind::Other;
        }
    }
}

__declspec(noinline) void LookupOf(LookupArgs* p_Args) {
    auto* s_Map = static_cast<RepoMap*>(p_Args->map);
    const SortedKey* s_Begin = p_Args->keys;
    const SortedKey* s_End = p_Args->keys + p_Args->count;

    for (const auto& s_Node : *s_Map) {
        if (fr::RepoPassOverran(++p_Args->scanned, p_Args->entryCount)) {
            p_Args->overran = true;
            return;
        }

        fr::Guid s_Guid;
        std::memcpy(&s_Guid, &s_Node.first, sizeof(s_Guid));

        const SortedKey* s_It = std::lower_bound(
            s_Begin, s_End, s_Guid, [](const SortedKey& p_Key, const fr::Guid& p_Guid) { return p_Key.guid < p_Guid; });

        for (; s_It != s_End && s_It->guid == s_Guid; ++s_It)
            FillLookup(s_Node.second, &p_Args->out[s_It->index]);
    }
}

__declspec(noinline) DWORD SafeLookup(LookupArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        LookupOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct ScanArgs {
    void* map;
    RepoParamEntry* out;
    size_t capacity;
    size_t count;
    size_t overflow;
    size_t scanned;
    size_t entryCount; // the map's size before the pass
    bool overran;      // more nodes than entryCount + 1: stopped (broken links)
};

// Fills p_Out from one entry; false when it has no ZString "Parameter".
bool FillParamEntry(const ZDynamicObject& p_Entry, RepoParamEntry* p_Out) {
    const EntryArray* s_Pairs = p_Entry.As<EntryArray>();

    if (!s_Pairs)
        return false;

    bool s_HasParameter = false;
    for (size_t i = 0; i < s_Pairs->size(); ++i) {
        const SDynamicObjectKeyValuePair& s_Pair = (*s_Pairs)[i];
        const std::string_view s_Key = s_Pair.sKey.ToStringView();

        if (s_Key == "Parameter") {
            if (const ZString* s_Text = s_Pair.value.As<ZString>()) {
                const std::string_view s_View = s_Text->ToStringView();
                CopyText(s_View.data(), s_View.size(), p_Out->parameter, sizeof(p_Out->parameter));
                p_Out->parameterCut = s_View.size() >= sizeof(p_Out->parameter);
                s_HasParameter = true;
            }
        }
        else if (s_Key == "Value") {
            p_Out->hasValue = true;

            if (const char* s_Name = TypeNameOf(s_Pair.value))
                CopyText(s_Name, std::strlen(s_Name), p_Out->valueType, sizeof(p_Out->valueType));

            if (float* s_Float = s_Pair.value.As<float>()) {
                p_Out->kind = RepoValueKind::Float32;
                p_Out->value = *s_Float;
                p_Out->valueData = s_Float;
            }
            else if (double* s_Double = s_Pair.value.As<double>()) {
                p_Out->kind = RepoValueKind::Float64;
                p_Out->value = *s_Double;
                p_Out->valueData = s_Double;
            }
            else {
                p_Out->kind = RepoValueKind::Other;
            }
        }
        else if (s_Key == "Toggle") {
            if (bool* s_Bool = s_Pair.value.As<bool>()) {
                p_Out->hasToggle = true;
                p_Out->toggle = *s_Bool;
                p_Out->toggleData = s_Bool;
            }
        }
    }

    return s_HasParameter;
}

__declspec(noinline) void ScanOf(ScanArgs* p_Args) {
    auto* s_Map = static_cast<RepoMap*>(p_Args->map);
    RepoParamEntry s_Entry;

    for (const auto& s_Node : *s_Map) {
        if (fr::RepoPassOverran(++p_Args->scanned, p_Args->entryCount)) {
            p_Args->overran = true;
            return;
        }

        std::memset(&s_Entry, 0, sizeof(s_Entry));
        std::memcpy(&s_Entry.guid, &s_Node.first, sizeof(s_Entry.guid));

        if (!FillParamEntry(s_Node.second, &s_Entry))
            continue;

        if (p_Args->count < p_Args->capacity)
            p_Args->out[p_Args->count++] = s_Entry;
        else
            ++p_Args->overflow;
    }
}

__declspec(noinline) DWORD SafeScan(ScanArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        ScanOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct ItemScanArgs {
    void* map;
    RepoItemEntry* out;
    size_t capacity;
    size_t count;
    size_t overflow;
    size_t scanned;
    size_t entryCount; // the map's size before the pass
    bool overran;      // more nodes than entryCount + 1: stopped (broken links)
};

// A ZString value as text, or its engine type name in angle brackets.
void CopyItemText(const ZObjectRef& p_Value, char* p_Out, size_t p_OutSize) {
    if (const ZString* s_Text = p_Value.As<ZString>()) {
        const std::string_view s_View = s_Text->ToStringView();
        CopyText(s_View.data(), s_View.size(), p_Out, p_OutSize);
        return;
    }

    const char* s_Name = TypeNameOf(p_Value);
    std::snprintf(p_Out, p_OutSize, "<%s>", s_Name ? s_Name : "unknown type");
}

// Fills p_Out from one entry; false when it is not an item.
bool FillItemEntry(const ZDynamicObject& p_Entry, RepoItemEntry* p_Out) {
    const EntryArray* s_Pairs = p_Entry.As<EntryArray>();

    if (!s_Pairs)
        return false;

    bool s_IsItem = false;

    for (size_t i = 0; i < s_Pairs->size(); ++i) {
        const SDynamicObjectKeyValuePair& s_Pair = (*s_Pairs)[i];
        const std::string_view s_Key = s_Pair.sKey.ToStringView();

        if (s_Key == "Title") {
            CopyItemText(s_Pair.value, p_Out->title, sizeof(p_Out->title));
        }
        else if (s_Key == "InventoryCategoryIcon") {
            CopyItemText(s_Pair.value, p_Out->icon, sizeof(p_Out->icon));
            s_IsItem = true;
        }
        else if (s_Key == "ItemType") {
            CopyItemText(s_Pair.value, p_Out->itemType, sizeof(p_Out->itemType));
            s_IsItem = true;
        }
    }

    return s_IsItem;
}

__declspec(noinline) void ItemScanOf(ItemScanArgs* p_Args) {
    auto* s_Map = static_cast<RepoMap*>(p_Args->map);
    RepoItemEntry s_Entry;

    for (const auto& s_Node : *s_Map) {
        if (fr::RepoPassOverran(++p_Args->scanned, p_Args->entryCount)) {
            p_Args->overran = true;
            return;
        }

        std::memset(&s_Entry, 0, sizeof(s_Entry));
        std::memcpy(&s_Entry.guid, &s_Node.first, sizeof(s_Entry.guid));

        if (!FillItemEntry(s_Node.second, &s_Entry))
            continue;

        if (p_Args->count < p_Args->capacity)
            p_Args->out[p_Args->count++] = s_Entry;
        else
            ++p_Args->overflow;
    }
}

__declspec(noinline) DWORD SafeItemScan(ItemScanArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        ItemScanOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct RarityScanArgs {
    void* map;
    RepoRarityEntry* out;
    size_t capacity;
    size_t count;
    size_t overflow;
    size_t scanned;
    size_t entryCount; // the map's size before the pass
    bool overran;      // more nodes than entryCount + 1: stopped (broken links)
};

// A ZString value as text; other types leave p_Out empty.
void CopyStringOnly(const ZObjectRef& p_Value, char* p_Out, size_t p_OutSize) {
    if (const ZString* s_Text = p_Value.As<ZString>()) {
        const std::string_view s_View = s_Text->ToStringView();
        CopyText(s_View.data(), s_View.size(), p_Out, p_OutSize);
    }
}

void ReadNumber(const ZObjectRef& p_Value, RepoValueKind* p_Kind, double* p_Number, void** p_Data) {
    if (float* s_Float = p_Value.As<float>()) {
        *p_Kind = RepoValueKind::Float32;
        *p_Number = *s_Float;
        *p_Data = s_Float;
    }
    else if (double* s_Double = p_Value.As<double>()) {
        *p_Kind = RepoValueKind::Float64;
        *p_Number = *s_Double;
        *p_Data = s_Double;
    }
    else {
        *p_Kind = RepoValueKind::Other;
    }
}

// Fills p_Out from one entry; false when it has no Evergreen_Rarity key.
bool FillRarityEntry(const ZDynamicObject& p_Entry, RepoRarityEntry* p_Out) {
    const EntryArray* s_Pairs = p_Entry.As<EntryArray>();

    if (!s_Pairs)
        return false;

    bool s_HasRarity = false;
    char s_CommonName[80] = {};

    for (size_t i = 0; i < s_Pairs->size(); ++i) {
        const SDynamicObjectKeyValuePair& s_Pair = (*s_Pairs)[i];
        const std::string_view s_Key = s_Pair.sKey.ToStringView();

        if (s_Key == "Evergreen_Rarity") {
            s_HasRarity = true;
            ReadNumber(s_Pair.value, &p_Out->rarityKind, &p_Out->rarity, &p_Out->rarityData);
        }
        else if (s_Key == "Evergreen_CapacityCost") {
            ReadNumber(s_Pair.value, &p_Out->capacityKind, &p_Out->capacity, &p_Out->capacityData);
        }
        else if (s_Key == "Title") {
            CopyStringOnly(s_Pair.value, p_Out->title, sizeof(p_Out->title));
        }
        else if (s_Key == "CommonName") {
            CopyStringOnly(s_Pair.value, s_CommonName, sizeof(s_CommonName));
        }
        else if (s_Key == "InventoryCategoryIcon") {
            CopyStringOnly(s_Pair.value, p_Out->icon, sizeof(p_Out->icon));
        }
        else if (s_Key == "AudioHeadType") {
            CopyStringOnly(s_Pair.value, p_Out->audioHead, sizeof(p_Out->audioHead));
        }
        else if (s_Key == "AudioTailType") {
            CopyStringOnly(s_Pair.value, p_Out->audioTail, sizeof(p_Out->audioTail));
        }
        else if (s_Key == "AudioExoticType") {
            CopyStringOnly(s_Pair.value, p_Out->audioExotic, sizeof(p_Out->audioExotic));
        }
    }

    if (!p_Out->title[0] && s_CommonName[0])
        std::memcpy(p_Out->title, s_CommonName, sizeof(p_Out->title));

    return s_HasRarity;
}

__declspec(noinline) void RarityScanOf(RarityScanArgs* p_Args) {
    auto* s_Map = static_cast<RepoMap*>(p_Args->map);
    RepoRarityEntry s_Entry;

    for (const auto& s_Node : *s_Map) {
        if (fr::RepoPassOverran(++p_Args->scanned, p_Args->entryCount)) {
            p_Args->overran = true;
            return;
        }

        std::memset(&s_Entry, 0, sizeof(s_Entry));
        std::memcpy(&s_Entry.guid, &s_Node.first, sizeof(s_Entry.guid));

        if (!FillRarityEntry(s_Node.second, &s_Entry))
            continue;

        if (p_Args->count < p_Args->capacity)
            p_Args->out[p_Args->count++] = s_Entry;
        else
            ++p_Args->overflow;
    }
}

__declspec(noinline) DWORD SafeRarityScan(RarityScanArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        RarityScanOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct LoadoutScanArgs {
    void* map;
    RepoLoadoutEntry* out;
    size_t capacity;
    size_t count;
    size_t overflow;
    size_t scanned;
    size_t entryCount; // the map's size before the pass
    bool overran;      // more nodes than entryCount + 1: stopped (broken links)
};

// A number (float32, float64, int32) as a double; false for anything else.
bool NumberOf(const ZObjectRef& p_Value, double* p_Out) {
    if (const float* s_Float = p_Value.As<float>()) {
        *p_Out = *s_Float;
        return true;
    }
    if (const double* s_Double = p_Value.As<double>()) {
        *p_Out = *s_Double;
        return true;
    }
    if (const int32_t* s_Int = p_Value.As<int32_t>()) {
        *p_Out = *s_Int;
        return true;
    }
    return false;
}

// Appends p_Text to the comma-separated p_List (cut to fit).
void AppendText(char* p_List, size_t p_Size, std::string_view p_Text) {
    size_t s_Length = std::strlen(p_List);

    if (s_Length && s_Length + 1 < p_Size)
        p_List[s_Length++] = ',';

    const size_t s_Count = std::min(p_Text.size(), p_Size - 1 - std::min(s_Length, p_Size - 1));
    std::memcpy(p_List + s_Length, p_Text.data(), s_Count);
    p_List[s_Length + s_Count] = '\0';
}

// An array of strings joined into p_Out; false when the value is no array of
// dynamic objects. Elements that are not strings are written as "?".
bool JoinStringArray(const ZObjectRef& p_Value, char* p_Out, size_t p_Size, size_t* p_Count) {
    const TArray<ZDynamicObject>* s_Array = p_Value.As<TArray<ZDynamicObject>>();

    if (!s_Array)
        return false;

    *p_Count = s_Array->size();

    for (size_t i = 0; i < s_Array->size(); ++i) {
        const ZString* s_Text = (*s_Array)[i].As<ZString>();
        AppendText(p_Out, p_Size, s_Text ? s_Text->ToStringView() : std::string_view("?"));
    }

    return true;
}

// Fills p_Out from one entry; false when it is no item.
bool FillLoadoutEntry(const ZDynamicObject& p_Entry, RepoLoadoutEntry* p_Out) {
    const EntryArray* s_Pairs = p_Entry.As<EntryArray>();

    if (!s_Pairs)
        return false;

    bool s_Item = false;
    p_Out->entry = &p_Entry;
    p_Out->loseOnWounded = -1;

    for (size_t i = 0; i < s_Pairs->size(); ++i) {
        const SDynamicObjectKeyValuePair& s_Pair = (*s_Pairs)[i];
        const std::string_view s_Key = s_Pair.sKey.ToStringView();

        if (s_Key == "Evergreen_CapacityCost") {
            p_Out->hasCapacity = NumberOf(s_Pair.value, &p_Out->capacity);
            s_Item = true;
        }
        else if (s_Key == "Evergreen_Rarity") {
            p_Out->hasRarity = NumberOf(s_Pair.value, &p_Out->rarity);
        }
        else if (s_Key == "InventoryCategoryIcon") {
            CopyStringOnly(s_Pair.value, p_Out->icon, sizeof(p_Out->icon));
            s_Item = true;
        }
        else if (s_Key == "OnlineTraits") {
            size_t s_Count = 0;
            JoinStringArray(s_Pair.value, p_Out->traits, sizeof(p_Out->traits), &s_Count);
            s_Item = true;
        }
        else if (s_Key == "Perks") {
            size_t s_Count = 0;
            JoinStringArray(s_Pair.value, p_Out->perks, sizeof(p_Out->perks), &s_Count);
        }
        else if (s_Key == "Title") {
            CopyStringOnly(s_Pair.value, p_Out->title, sizeof(p_Out->title));
        }
        else if (s_Key == "CommonName") {
            CopyStringOnly(s_Pair.value, p_Out->commonName, sizeof(p_Out->commonName));
        }
        else if (s_Key == "ItemType") {
            CopyStringOnly(s_Pair.value, p_Out->itemType, sizeof(p_Out->itemType));
        }
        else if (s_Key == "AudioHeadType") {
            CopyStringOnly(s_Pair.value, p_Out->audioHead, sizeof(p_Out->audioHead));
        }
        else if (s_Key == "AudioTailType") {
            CopyStringOnly(s_Pair.value, p_Out->audioTail, sizeof(p_Out->audioTail));
        }
        else if (s_Key == "AudioExoticType") {
            CopyStringOnly(s_Pair.value, p_Out->audioExotic, sizeof(p_Out->audioExotic));
        }
        else if (s_Key == "ItemSize") {
            CopyStringOnly(s_Pair.value, p_Out->itemSize, sizeof(p_Out->itemSize));
        }
        else if (s_Key == "GripAnimType") {
            CopyStringOnly(s_Pair.value, p_Out->grip, sizeof(p_Out->grip));
        }
        else if (s_Key == "HolsterAbility") {
            CopyStringOnly(s_Pair.value, p_Out->holster, sizeof(p_Out->holster));
        }
        else if (s_Key == "Runtime") {
            CopyStringOnly(s_Pair.value, p_Out->runtime, sizeof(p_Out->runtime));
        }
        else if (s_Key == "Evergreen_UnlockEntitlement") {
            CopyStringOnly(s_Pair.value, p_Out->entitlement, sizeof(p_Out->entitlement));
        }
        else if (s_Key == "Evergreen_LoseOnWounded") {
            const bool* s_Bool = s_Pair.value.As<bool>();
            p_Out->loseOnWounded = s_Bool ? (*s_Bool ? 1 : 0) : -1;
        }
    }

    if (!p_Out->title[0] && p_Out->commonName[0])
        std::memcpy(p_Out->title, p_Out->commonName, sizeof(p_Out->title));

    return s_Item;
}

__declspec(noinline) void LoadoutScanOf(LoadoutScanArgs* p_Args) {
    auto* s_Map = static_cast<RepoMap*>(p_Args->map);
    RepoLoadoutEntry s_Entry;

    for (const auto& s_Node : *s_Map) {
        if (fr::RepoPassOverran(++p_Args->scanned, p_Args->entryCount)) {
            p_Args->overran = true;
            return;
        }

        std::memset(&s_Entry, 0, sizeof(s_Entry));
        std::memcpy(&s_Entry.guid, &s_Node.first, sizeof(s_Entry.guid));

        if (!FillLoadoutEntry(s_Node.second, &s_Entry))
            continue;

        if (p_Args->count < p_Args->capacity)
            p_Args->out[p_Args->count++] = s_Entry;
        else
            ++p_Args->overflow;
    }
}

__declspec(noinline) DWORD SafeLoadoutScan(LoadoutScanArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        LoadoutScanOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct ConcealScanArgs {
    void* map;
    RepoConcealEntry* out;
    size_t capacity;
    size_t count;
    size_t overflow;
    size_t scanned;
    size_t entryCount; // the map's size before the pass
    bool overran;      // more nodes than entryCount + 1: stopped (broken links)
};

int8_t BoolFlag(const ZObjectRef& p_Value) {
    const bool* s_Bool = p_Value.As<bool>();
    return s_Bool ? (*s_Bool ? 1 : 0) : -1;
}

// Fills p_Out from one entry; false when it has no InventoryCategoryIcon.
bool FillConcealEntry(ZDynamicObject& p_Entry, RepoConcealEntry* p_Out) {
    EntryArray* s_Pairs = p_Entry.As<EntryArray>();

    if (!s_Pairs)
        return false;

    bool s_Item = false;
    p_Out->canConceal = -1;
    p_Out->frisked = -1;

    for (size_t i = 0; i < s_Pairs->size(); ++i) {
        SDynamicObjectKeyValuePair& s_Pair = (*s_Pairs)[i];
        const std::string_view s_Key = s_Pair.sKey.ToStringView();

        if (s_Key == "InventoryCategoryIcon") {
            CopyStringOnly(s_Pair.value, p_Out->icon, sizeof(p_Out->icon));
            s_Item = true;
        }
        else if (s_Key == "Title") {
            CopyStringOnly(s_Pair.value, p_Out->title, sizeof(p_Out->title));
        }
        else if (s_Key == "CommonName") {
            CopyStringOnly(s_Pair.value, p_Out->commonName, sizeof(p_Out->commonName));
        }
        else if (s_Key == "ItemSize") {
            if (ZString* s_Text = s_Pair.value.As<ZString>()) {
                p_Out->itemSizeValue = s_Text;
                const std::string_view s_View = s_Text->ToStringView();
                CopyText(s_View.data(), s_View.size(), p_Out->itemSize, sizeof(p_Out->itemSize));
            }
        }
        else if (s_Key == "HolsterAbility") {
            if (ZString* s_Text = s_Pair.value.As<ZString>()) {
                p_Out->holsterValue = s_Text;
                const std::string_view s_View = s_Text->ToStringView();
                CopyText(s_View.data(), s_View.size(), p_Out->holster, sizeof(p_Out->holster));
            }
        }
        else if (s_Key == "HitmanCanConceal") {
            p_Out->canConceal = BoolFlag(s_Pair.value);
        }
        else if (s_Key == "IsCheckedWhenFrisked") {
            p_Out->frisked = BoolFlag(s_Pair.value);
        }
    }

    return s_Item;
}

__declspec(noinline) void ConcealScanOf(ConcealScanArgs* p_Args) {
    auto* s_Map = static_cast<RepoMap*>(p_Args->map);
    RepoConcealEntry s_Entry;

    for (auto& s_Node : *s_Map) {
        if (fr::RepoPassOverran(++p_Args->scanned, p_Args->entryCount)) {
            p_Args->overran = true;
            return;
        }

        std::memset(&s_Entry, 0, sizeof(s_Entry));
        std::memcpy(&s_Entry.guid, &s_Node.first, sizeof(s_Entry.guid));

        if (!FillConcealEntry(s_Node.second, &s_Entry))
            continue;

        if (p_Args->count < p_Args->capacity)
            p_Args->out[p_Args->count++] = s_Entry;
        else
            ++p_Args->overflow;
    }
}

__declspec(noinline) DWORD SafeConcealScan(ConcealScanArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        ConcealScanOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void CopyStringOf(ZString* p_To, const ZString* p_From) {
    *p_To = *p_From;
}

__declspec(noinline) DWORD SafeCopyString(ZString* p_To, const ZString* p_From) {
    DWORD s_Code = 0;

    __try {
        CopyStringOf(p_To, p_From);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct PairsArgs {
    const void* entry;
    RepoPairText* out;
    size_t capacity;
    size_t count;
};

__declspec(noinline) void PairsOf(PairsArgs* p_Args) {
    const EntryArray* s_Pairs = static_cast<const ZDynamicObject*>(p_Args->entry)->As<EntryArray>();

    if (!s_Pairs)
        return;

    for (size_t i = 0; i < s_Pairs->size() && p_Args->count < p_Args->capacity; ++i) {
        const SDynamicObjectKeyValuePair& s_Source = (*s_Pairs)[i];
        RepoPairText& s_Out = p_Args->out[p_Args->count++];
        std::memset(&s_Out, 0, sizeof(s_Out));
        const std::string_view s_Key = s_Source.sKey.ToStringView();
        CopyText(s_Key.data(), s_Key.size(), s_Out.key, sizeof(s_Out.key));
        const ZObjectRef& s_Value = s_Source.value;
        double s_Number = 0.0;
        size_t s_Count = 0;

        if (const ZString* s_Text = s_Value.As<ZString>()) {
            const std::string_view s_View = s_Text->ToStringView();
            CopyText(s_View.data(), s_View.size(), s_Out.value, sizeof(s_Out.value));
        }
        else if (const bool* s_Bool = s_Value.As<bool>()) {
            std::snprintf(s_Out.value, sizeof(s_Out.value), "%s", *s_Bool ? "true" : "false");
        }
        else if (NumberOf(s_Value, &s_Number)) {
            std::snprintf(s_Out.value, sizeof(s_Out.value), "%g", s_Number);
        }
        else if (const uint32_t* s_UInt = s_Value.As<uint32_t>()) {
            std::snprintf(s_Out.value, sizeof(s_Out.value), "%u", *s_UInt);
        }
        else if (JoinStringArray(s_Value, s_Out.value + 1, sizeof(s_Out.value) - 2, &s_Count)) {
            s_Out.value[0] = '[';
            const size_t s_Length = std::strlen(s_Out.value);
            s_Out.value[s_Length] = ']';
            s_Out.value[s_Length + 1] = '\0';
        }
        else if (const EntryArray* s_Object = s_Value.As<EntryArray>()) {
            std::snprintf(s_Out.value, sizeof(s_Out.value), "<object of %zu keys>", s_Object->size());
        }
        else if (const char* s_Type = TypeNameOf(s_Value)) {
            std::snprintf(s_Out.value, sizeof(s_Out.value), "<%s>", s_Type);
        }
        else {
            std::snprintf(s_Out.value, sizeof(s_Out.value), "<?>");
        }
    }
}

__declspec(noinline) DWORD SafePairs(PairsArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        PairsOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void WriteValueOf(void* p_Data, RepoValueKind p_Kind, double p_Value) {
    if (p_Kind == RepoValueKind::Float32)
        *static_cast<float*>(p_Data) = static_cast<float>(p_Value);
    else if (p_Kind == RepoValueKind::Float64)
        *static_cast<double*>(p_Data) = p_Value;
}

__declspec(noinline) DWORD SafeWriteValue(void* p_Data, RepoValueKind p_Kind, double p_Value) {
    DWORD s_Code = 0;

    __try {
        WriteValueOf(p_Data, p_Kind, p_Value);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void WriteToggleOf(bool* p_Data, bool p_Value) {
    *p_Data = p_Value;
}

__declspec(noinline) DWORD SafeWriteToggle(bool* p_Data, bool p_Value) {
    DWORD s_Code = 0;

    __try {
        WriteToggleOf(p_Data, p_Value);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

std::string RepoUnwritableReason(const RepoLookup& p_Lookup) {
    if (!p_Lookup.entryFound)
        return "not in pro.repo";

    if (!p_Lookup.entryIsObject)
        return "entry is not a key-value object";

    if (!p_Lookup.hasValue)
        return "no Value key";

    if (p_Lookup.kind == RepoValueKind::Float32 || p_Lookup.kind == RepoValueKind::Float64)
        return {};

    return fmt::format("Value is {}, not float32/float64", p_Lookup.typeName[0] ? p_Lookup.typeName : "<unknown type>");
}

bool ProRepository::Ready(std::string& p_Why) {
    if (!Globals::ResourceManager || !Globals::ResourceContainer || !*Globals::ResourceContainer) {
        p_Why = "resource manager not available";
        return false;
    }

    if (m_Resource.m_nResourceIndex.val == -1)
        Globals::ResourceManager->GetResourcePtr(m_Resource, kProRepo, 0);

    if (m_Resource.m_nResourceIndex.val == -1) {
        p_Why = "pro.repo resource not found";
        return false;
    }

    ResourceState s_State {};

    if (const DWORD s_Code = SafeResourceState(&m_Resource, &s_State)) {
        p_Why = fmt::format("fault reading the pro.repo resource state (exception 0x{:08X})",
                            static_cast<uint32_t>(s_Code));
        return false;
    }

    if (s_State.status != RESOURCE_STATUS_VALID || !s_State.data) {
        p_Why = fmt::format("pro.repo not loaded (resource status {})", s_State.status);
        return false;
    }

    return true;
}

void* ProRepository::MapPointer() {
    if (m_Resource.m_nResourceIndex.val == -1)
        return nullptr;

    ResourceState s_State {};

    if (SafeResourceState(&m_Resource, &s_State) || s_State.status != RESOURCE_STATUS_VALID)
        return nullptr;

    return s_State.data;
}

size_t ProRepository::EntryCount() {
    void* s_Map = MapPointer();
    size_t s_Count = 0;

    if (!s_Map || SafeEntryCount(s_Map, &s_Count))
        return 0;

    return s_Count;
}

uint32_t ProRepository::Lookup(std::span<const fr::Guid> p_Keys, std::vector<RepoLookup>& p_Out) {
    p_Out.assign(p_Keys.size(), RepoLookup {});
    void* s_Map = MapPointer();

    if (!s_Map || p_Keys.empty())
        return 0;

    std::vector<SortedKey> s_Keys;
    s_Keys.reserve(p_Keys.size());

    for (size_t i = 0; i < p_Keys.size(); ++i)
        s_Keys.push_back({p_Keys[i], i});

    std::sort(s_Keys.begin(), s_Keys.end(),
              [](const SortedKey& p_A, const SortedKey& p_B) { return p_A.guid < p_B.guid; });

    size_t s_EntryCount = 0;

    if (const DWORD s_Code = SafeEntryCount(s_Map, &s_EntryCount)) {
        if (!m_LoggedFault) {
            m_LoggedFault = true;
            Log("pro.repo: fault reading the entry count before a lookup pass (exception 0x{:08X}); nothing looked up",
                static_cast<uint32_t>(s_Code));
        }

        return s_Code;
    }

    LookupArgs s_Args {s_Map, s_Keys.data(), s_Keys.size(), p_Out.data(), s_EntryCount, 0, false};
    DWORD s_Code = SafeLookup(&s_Args);

    if (!s_Code && s_Args.overran) {
        s_Code = kOverrunCode;

        if (!m_LoggedFault) {
            m_LoggedFault = true;
            Log("pro.repo: fault: a lookup pass visited more than {} + 1 entries (the map's links are broken); "
                "stopped, results of that pass are partial",
                s_EntryCount);
        }
    }
    else if (s_Code && !m_LoggedFault) {
        m_LoggedFault = true;
        Log("pro.repo: fault during a lookup pass (exception 0x{:08X}); results of that pass are partial",
            static_cast<uint32_t>(s_Code));
    }

    return s_Code;
}

uint32_t ProRepository::Write(const RepoLookup& p_Lookup, double p_Value) {
    if (!p_Lookup.valueData || (p_Lookup.kind != RepoValueKind::Float32 && p_Lookup.kind != RepoValueKind::Float64)) {
        return 0;
    }

    return SafeWriteValue(p_Lookup.valueData, p_Lookup.kind, p_Value);
}

uint32_t ProRepository::Write(const RepoParamEntry& p_Entry, double p_Value) {
    if (!p_Entry.valueData || (p_Entry.kind != RepoValueKind::Float32 && p_Entry.kind != RepoValueKind::Float64))
        return 0;

    return SafeWriteValue(p_Entry.valueData, p_Entry.kind, p_Value);
}

uint32_t ProRepository::WriteToggle(const RepoLookup& p_Lookup, bool p_Value) {
    return p_Lookup.toggleData ? SafeWriteToggle(p_Lookup.toggleData, p_Value) : 0;
}

uint32_t ProRepository::WriteToggle(const RepoParamEntry& p_Entry, bool p_Value) {
    return p_Entry.toggleData ? SafeWriteToggle(p_Entry.toggleData, p_Value) : 0;
}

uint32_t ProRepository::ScanParameters(std::vector<RepoParamEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned) {
    p_Out.clear();
    *p_Overflow = 0;
    *p_Scanned = 0;
    void* s_Map = MapPointer();

    if (!s_Map)
        return 0;

    size_t s_EntryCount = 0;

    if (const DWORD s_Code = SafeEntryCount(s_Map, &s_EntryCount)) {
        Log("pro.repo: fault reading the entry count before a Parameter scan (exception 0x{:08X}); nothing scanned",
            static_cast<uint32_t>(s_Code));
        return s_Code;
    }

    p_Out.resize(kMaxParamEntries);
    ScanArgs s_Args {s_Map, p_Out.data(), p_Out.size(), 0, 0, 0, s_EntryCount, false};
    DWORD s_Code = SafeScan(&s_Args);
    p_Out.resize(s_Args.count);
    *p_Overflow = s_Args.overflow;
    *p_Scanned = s_Args.scanned;

    if (!s_Code && s_Args.overran) {
        s_Code = kOverrunCode;
        Log("pro.repo: fault: a Parameter scan visited more than {} + 1 entries (the map's links are broken); "
            "stopped, results are partial",
            s_EntryCount);
    }
    else if (s_Code) {
        Log("pro.repo: fault during a Parameter scan (exception 0x{:08X}) after {} entries; results are partial",
            static_cast<uint32_t>(s_Code), s_Args.scanned);
    }

    return s_Code;
}

uint32_t ProRepository::ScanItems(std::vector<RepoItemEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned) {
    p_Out.clear();
    *p_Overflow = 0;
    *p_Scanned = 0;
    void* s_Map = MapPointer();

    if (!s_Map)
        return 0;

    size_t s_EntryCount = 0;

    if (const DWORD s_Code = SafeEntryCount(s_Map, &s_EntryCount)) {
        Log("pro.repo: fault reading the entry count before an item scan (exception 0x{:08X}); nothing scanned",
            static_cast<uint32_t>(s_Code));
        return s_Code;
    }

    p_Out.resize(kMaxItemEntries);
    ItemScanArgs s_Args {s_Map, p_Out.data(), p_Out.size(), 0, 0, 0, s_EntryCount, false};
    DWORD s_Code = SafeItemScan(&s_Args);
    p_Out.resize(s_Args.count);
    *p_Overflow = s_Args.overflow;
    *p_Scanned = s_Args.scanned;

    if (!s_Code && s_Args.overran) {
        s_Code = kOverrunCode;
        Log("pro.repo: fault: an item scan visited more than {} + 1 entries (the map's links are broken); stopped, "
            "results are partial",
            s_EntryCount);
    }
    else if (s_Code) {
        Log("pro.repo: fault during an item scan (exception 0x{:08X}) after {} entries; results are partial",
            static_cast<uint32_t>(s_Code), s_Args.scanned);
    }

    return s_Code;
}

uint32_t ProRepository::ScanRarityItems(std::vector<RepoRarityEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned) {
    p_Out.clear();
    *p_Overflow = 0;
    *p_Scanned = 0;
    void* s_Map = MapPointer();

    if (!s_Map)
        return 0;

    size_t s_EntryCount = 0;

    if (const DWORD s_Code = SafeEntryCount(s_Map, &s_EntryCount)) {
        Log("pro.repo: fault reading the entry count before a rarity scan (exception 0x{:08X}); nothing scanned",
            static_cast<uint32_t>(s_Code));
        return s_Code;
    }

    p_Out.resize(kMaxItemEntries);
    RarityScanArgs s_Args {s_Map, p_Out.data(), p_Out.size(), 0, 0, 0, s_EntryCount, false};
    DWORD s_Code = SafeRarityScan(&s_Args);
    p_Out.resize(s_Args.count);
    *p_Overflow = s_Args.overflow;
    *p_Scanned = s_Args.scanned;

    if (!s_Code && s_Args.overran) {
        s_Code = kOverrunCode;
        Log("pro.repo: fault: a rarity scan visited more than {} + 1 entries (the map's links are broken); "
            "stopped, results are partial",
            s_EntryCount);
    }
    else if (s_Code) {
        Log("pro.repo: fault during a rarity scan (exception 0x{:08X}) after {} entries; results are partial",
            static_cast<uint32_t>(s_Code), s_Args.scanned);
    }

    return s_Code;
}

uint32_t ProRepository::ScanLoadoutItems(std::vector<RepoLoadoutEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned) {
    p_Out.clear();
    *p_Overflow = 0;
    *p_Scanned = 0;
    void* s_Map = MapPointer();

    if (!s_Map)
        return 0;

    size_t s_EntryCount = 0;

    if (const DWORD s_Code = SafeEntryCount(s_Map, &s_EntryCount)) {
        Log("pro.repo: fault reading the entry count before a loadout item scan (exception 0x{:08X}); nothing scanned",
            static_cast<uint32_t>(s_Code));
        return s_Code;
    }

    p_Out.resize(kMaxItemEntries);
    LoadoutScanArgs s_Args {s_Map, p_Out.data(), p_Out.size(), 0, 0, 0, s_EntryCount, false};
    DWORD s_Code = SafeLoadoutScan(&s_Args);
    p_Out.resize(s_Args.count);
    *p_Overflow = s_Args.overflow;
    *p_Scanned = s_Args.scanned;

    if (!s_Code && s_Args.overran) {
        s_Code = kOverrunCode;
        Log("pro.repo: fault: a loadout item scan visited more than {} + 1 entries (the map's links are broken); "
            "stopped, results are partial",
            s_EntryCount);
    }
    else if (s_Code) {
        Log("pro.repo: fault during a loadout item scan (exception 0x{:08X}) after {} entries; results are partial",
            static_cast<uint32_t>(s_Code), s_Args.scanned);
    }

    return s_Code;
}

uint32_t ProRepository::DumpPairs(const void* p_Entry, std::vector<RepoPairText>& p_Out) {
    p_Out.clear();

    if (!p_Entry)
        return 0;

    p_Out.resize(kMaxDumpPairs);
    PairsArgs s_Args {p_Entry, p_Out.data(), p_Out.size(), 0};
    const DWORD s_Code = SafePairs(&s_Args);
    p_Out.resize(s_Args.count);
    return s_Code;
}

uint32_t ProRepository::WriteValue(void* p_Data, RepoValueKind p_Kind, double p_Value) {
    if (!p_Data || (p_Kind != RepoValueKind::Float32 && p_Kind != RepoValueKind::Float64))
        return 0;

    return SafeWriteValue(p_Data, p_Kind, p_Value);
}

uint32_t ProRepository::ScanConcealItems(std::vector<RepoConcealEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned) {
    p_Out.clear();
    *p_Overflow = 0;
    *p_Scanned = 0;
    void* s_Map = MapPointer();

    if (!s_Map)
        return 0;

    size_t s_EntryCount = 0;

    if (const DWORD s_Code = SafeEntryCount(s_Map, &s_EntryCount)) {
        Log("pro.repo: fault reading the entry count before a conceal item scan (exception 0x{:08X}); nothing scanned",
            static_cast<uint32_t>(s_Code));
        return s_Code;
    }

    p_Out.resize(kMaxItemEntries);
    ConcealScanArgs s_Args {s_Map, p_Out.data(), p_Out.size(), 0, 0, 0, s_EntryCount, false};
    DWORD s_Code = SafeConcealScan(&s_Args);
    p_Out.resize(s_Args.count);
    *p_Overflow = s_Args.overflow;
    *p_Scanned = s_Args.scanned;

    if (!s_Code && s_Args.overran) {
        s_Code = kOverrunCode;
        Log("pro.repo: fault: a conceal item scan visited more than {} + 1 entries (the map's links are broken); "
            "stopped, results are partial",
            s_EntryCount);
    }
    else if (s_Code) {
        Log("pro.repo: fault during a conceal item scan (exception 0x{:08X}) after {} entries; results are partial",
            static_cast<uint32_t>(s_Code), s_Args.scanned);
    }

    return s_Code;
}

uint32_t ProRepository::CopyString(ZString* p_To, const ZString* p_From) {
    if (!p_To || !p_From || p_To == p_From)
        return 0;

    return SafeCopyString(p_To, p_From);
}
