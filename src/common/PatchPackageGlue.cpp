#include "PatchPackageGlue.h"

#include "Guard.h"

#include <Globals.h>

#include <Glacier/ZResource.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZString.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

namespace pkg {

namespace {

void CopyName(const ZString& p_Name, char* p_Out, size_t p_Size) {
    const std::string_view s_Name = p_Name.ToStringView();
    const size_t s_Length = std::min(s_Name.size(), p_Size - 1);
    std::memcpy(p_Out, s_Name.data(), s_Length);
    p_Out[s_Length] = '\0';
}

__declspec(noinline) void ResourceChainOf(uint64_t p_Rid, ResourceChainOut* p_Out) {
    ZResourceContainer* s_Container = Globals::ResourceContainer ? *Globals::ResourceContainer : nullptr;

    if (!s_Container)
        return;

    p_Out->container = true;

    const ZRuntimeResourceID s_Rid(p_Rid);
    int s_Index = -1;
    const auto s_It = s_Container->m_indices.find(s_Rid);

    if (s_It != s_Container->m_indices.end()) {
        s_Index = s_It->second.val;
    }
    else {
        const size_t s_Count = s_Container->m_resources.size();

        for (size_t i = 0; i < s_Count; ++i) {
            if (s_Container->m_resources[i].rid.GetID() == p_Rid) {
                s_Index = static_cast<int>(i);
                break;
            }
        }
    }

    if (s_Index < 0)
        return;

    p_Out->found = true;

    while (s_Index >= 0 && static_cast<size_t>(s_Index) < s_Container->m_resources.size() &&
           p_Out->hops < static_cast<int>(std::size(p_Out->hop))) {
        const ZResourceContainer::SResourceInfo& s_Info = s_Container->m_resources[s_Index];
        ResourceHop& s_Hop = p_Out->hop[p_Out->hops++];
        const uint32_t s_Package = static_cast<uint8_t>(s_Info.packageId);
        s_Hop.dataSize = s_Info.dataSize;
        s_Hop.package[0] = '\0';

        if (s_Package < s_Container->m_MountedPackages.size())
            CopyName(s_Container->m_MountedPackages[s_Package], s_Hop.package, sizeof(s_Hop.package));

        const int s_Next = s_Info.nextNewestIndex.val;

        if (s_Next == s_Index)
            break;

        s_Index = s_Next;
    }
}

} // namespace

std::filesystem::path RuntimeFolder() {
    std::wstring s_Exe(32768, L'\0');
    const DWORD s_Length = GetModuleFileNameW(nullptr, s_Exe.data(), static_cast<DWORD>(s_Exe.size()));
    s_Exe.resize(s_Length);
    return std::filesystem::path(s_Exe).parent_path().parent_path() / "Runtime";
}

__declspec(noinline) DWORD SafeResourceChain(uint64_t p_Rid, ResourceChainOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ResourceChainOf(p_Rid, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace pkg
