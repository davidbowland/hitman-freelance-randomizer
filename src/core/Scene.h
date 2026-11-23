#pragma once

#include <string>
#include <string_view>

namespace fr {

// ASCII lower-case copy of s.
std::string ToLower(std::string_view s);

// The resource path whose hash (ZRuntimeResourceID::FromString) is a brick's
// or scene's runtime resource id: "assembly:/a/b.brick" ->
// "[assembly:/a/b.brick].pc_entitytype", ASCII lowercase (a path already in
// brackets is only lowercased).
std::string BrickResourcePath(std::string_view path);

// The same for a scene, whose TEMP is its "[path].pc_entitytemplate"
// (scene_costaltown_mild_bingo.entity -> 004B6755A8F753FA).
std::string SceneResourcePath(std::string_view path);

// A Freelancer mission: type "evergreen" and the scene resource (any case) is
// not under /snug/ (the safehouse).
bool IsFreelancerMission(std::string_view type, std::string_view sceneResource);

// Any Freelancer scene, mission or safehouse: type "evergreen".
bool IsFreelancerScene(std::string_view type);

} // namespace fr
