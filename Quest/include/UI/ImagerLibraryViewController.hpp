#pragma once

#include "HMUI/ViewController.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Transform.hpp"

#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(Imager::UI, ImagerLibraryViewController, HMUI::ViewController) {
    DECLARE_SIMPLE_DTOR();

    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidDeactivate, &HMUI::ViewController::DidDeactivate, bool removedFromHierarchy, bool screenSystemDisabling);

    // bsml id bindings
    DECLARE_INSTANCE_FIELD(UnityEngine::Transform*, gridHost);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, emptyText);

    DECLARE_INSTANCE_METHOD(void, RefreshGrid);
};
