#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace UnityEngine {
    class Sprite;
}

namespace Imager::UI::LibraryState {
    void Refresh();

    std::vector<std::string> const& GetImages();

    UnityEngine::Sprite* GetOrCreateThumbnail(std::string_view fileName);

    void ClearThumbnailCache();
}
