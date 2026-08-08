#pragma once

#include <functional>
#include <string>

namespace Imager::UI {
    using Callback = std::function<void()>;
    using PlacementCallback = std::function<void(std::string const& placementId)>;

    void RegisterLibraryChangedCallback(void* key, Callback cb);
    void UnregisterLibraryChangedCallback(void* key);
    void NotifyLibraryChanged();

    void RegisterEditPlacementRequestedCallback(void* key, PlacementCallback cb);
    void UnregisterEditPlacementRequestedCallback(void* key);
    void NotifyEditPlacementRequested(std::string const& placementId);
}
