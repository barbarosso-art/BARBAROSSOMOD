#pragma once

#include "custom-types/shared/macros.hpp"

#include "UnityEngine/MonoBehaviour.hpp"

namespace UnityEngine {
    class GameObject;
}

DECLARE_CLASS_CODEGEN(Imager, PlacedImage, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(StringW, placementId);
    DECLARE_INSTANCE_FIELD(StringW, fileName);
};
