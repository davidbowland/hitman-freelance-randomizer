#pragma once

// Log text for engine values: faults, ids, entities and scenes.

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

// "<fault 0xC0000005>": a guarded call's structured-exception code.
inline std::string FaultText(uint32_t p_Code) {
    char s_Text[24];
    std::snprintf(s_Text, sizeof(s_Text), "<fault 0x%08X>", static_cast<unsigned>(p_Code));
    return s_Text;
}

// "00a1b2c3d4e5f607": a sub-entity or resource id.
inline std::string IdText(uint64_t p_Id) {
    char s_Text[24];
    std::snprintf(s_Text, sizeof(s_Text), "%016llx", static_cast<unsigned long long>(p_Id));
    return s_Text;
}

// "<id> (<class>)" of an entity, from its sub-entity id and class name
// (ReadEntityClassName); "<no class>" when the name is empty.
inline std::string EntityIdText(uint64_t p_Id, std::string_view p_Class) {
    return IdText(p_Id) + " (" + std::string(p_Class.empty() ? std::string_view("<no class>") : p_Class) + ")";
}

// A scene's file name without its folder and ".entity"
// ("assembly:/_pro/scenes/missions/paris/_scene_paris.entity" -> "_scene_paris").
inline std::string ShortSceneName(std::string_view p_Scene) {
    const size_t s_Slash = p_Scene.find_last_of('/');

    if (s_Slash != std::string_view::npos)
        p_Scene.remove_prefix(s_Slash + 1);

    if (p_Scene.ends_with(".entity"))
        p_Scene.remove_suffix(std::string_view(".entity").size());

    return std::string(p_Scene);
}
