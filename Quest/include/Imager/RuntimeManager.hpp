#pragma once

#include <string>
#include <string_view>

#include "custom-types/shared/macros.hpp"

#include "UnityEngine/MonoBehaviour.hpp"

namespace UnityEngine {
    class GameObject;
    class Transform;
}

namespace Imager {
    struct PlacementInfo {
        std::string id;
        std::string fileName;
        std::string name;

        float posX = 0.0f;
        float posY = 0.0f;
        float posZ = 0.0f;

        float rotX = 0.0f;
        float rotY = 0.0f;
        float rotZ = 0.0f;

        float width = 0.0f;
        float height = 0.0f;

        bool showInMenu = true;
        bool showInGame = true;
    };

    void EnsureRuntimeManager();

    void SpawnSavedPlacements();

    /// Loads the selected custom map's own image. Nothing is persisted in a
    /// global placements list, so the art cannot leak into another song.
    void LoadMapPlacementForSong(std::string_view songName);

    void SummonImage(std::string_view fileName);

    void SetEditMode(bool enabled);
    bool GetEditMode();

    void RemovePlacement(std::string_view placementId);

    void ClearAllPlacements();

    void RemovePlacementsForFile(std::string_view fileName);

    bool TryGetPlacementInfo(std::string_view placementId, PlacementInfo& outInfo);
    bool SetPlacementName(std::string_view placementId, std::string_view name);
    bool SetPlacementShowInMenu(std::string_view placementId, bool value);
    bool SetPlacementShowInGame(std::string_view placementId, bool value);
    bool SetPlacementPosition(std::string_view placementId, float x, float y, float z);
    bool SetPlacementRotation(std::string_view placementId, float x, float y, float z);
    bool SetPlacementSize(std::string_view placementId, float width, float height);
    bool RecenterPlacement(std::string_view placementId);
}

DECLARE_CLASS_CODEGEN(Imager, RuntimeManager, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_METHOD(void, Awake);
    DECLARE_INSTANCE_METHOD(void, Update);
};
