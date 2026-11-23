#include "Scene.h"

namespace fr {

std::string ToLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string BrickResourcePath(std::string_view path) {
    std::string lower = ToLower(path);

    if (!lower.empty() && lower.front() == '[')
        return lower;

    return "[" + lower + "].pc_entitytype";
}

std::string SceneResourcePath(std::string_view path) {
    std::string lower = ToLower(path);

    if (!lower.empty() && lower.front() == '[')
        return lower;

    return "[" + lower + "].pc_entitytemplate";
}

bool IsFreelancerMission(std::string_view type, std::string_view sceneResource) {
    return IsFreelancerScene(type) && ToLower(sceneResource).find("/snug/") == std::string::npos;
}

bool IsFreelancerScene(std::string_view type) {
    return type == "evergreen";
}

} // namespace fr
