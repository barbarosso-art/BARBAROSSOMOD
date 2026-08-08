#include "UI/AppEvents.hpp"

#include <unordered_map>

namespace Imager::UI {
    static std::unordered_map<void*, Callback> g_libraryChanged;
    static std::unordered_map<void*, PlacementCallback> g_editPlacementRequested;

    void RegisterLibraryChangedCallback(void* key, Callback cb) {
        if (!key) return;
        g_libraryChanged[key] = std::move(cb);
    }

    void UnregisterLibraryChangedCallback(void* key) {
        if (!key) return;
        g_libraryChanged.erase(key);
    }

    void NotifyLibraryChanged() {
        auto copy = g_libraryChanged;
        for (auto const& [key, cb] : copy) {
            (void)key;
            if (cb) cb();
        }
    }

    void RegisterEditPlacementRequestedCallback(void* key, PlacementCallback cb) {
        if (!key) return;
        g_editPlacementRequested[key] = std::move(cb);
    }

    void UnregisterEditPlacementRequestedCallback(void* key) {
        if (!key) return;
        g_editPlacementRequested.erase(key);
    }

    void NotifyEditPlacementRequested(std::string const& placementId) {
        auto copy = g_editPlacementRequested;
        for (auto const& [key, cb] : copy) {
            (void)key;
            if (cb) cb(placementId);
        }
    }
}
