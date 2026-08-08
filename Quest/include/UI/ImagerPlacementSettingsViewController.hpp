#pragma once

#include "HMUI/InputFieldView.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Transform.hpp"

#include "beatsaber-hook/shared/utils/typedefs-string.hpp"

#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(Imager::UI, ImagerPlacementSettingsViewController, HMUI::ViewController) {
    DECLARE_SIMPLE_DTOR();

    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidDeactivate, &HMUI::ViewController::DidDeactivate, bool removedFromHierarchy, bool screenSystemDisabling);

    // bsml id bindings
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, placementInfoText);
    DECLARE_INSTANCE_FIELD(UnityEngine::Transform*, settingsRoot);
    DECLARE_INSTANCE_FIELD(UnityEngine::Transform*, nameKeyboardHost);

    DECLARE_INSTANCE_FIELD(HMUI::InputFieldView*, nameInput);
    DECLARE_INSTANCE_FIELD(BSML::ToggleSetting*, showInMenuToggle);
    DECLARE_INSTANCE_FIELD(BSML::ToggleSetting*, showInGameToggle);

    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, widthSetting);
    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, heightSetting);
    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, posXSetting);
    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, posYSetting);
    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, posZSetting);
    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, rotXSetting);
    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, rotYSetting);
    DECLARE_INSTANCE_FIELD(BSML::IncrementSetting*, rotZSetting);

    DECLARE_INSTANCE_FIELD(BSML::ModalView*, visibilityModal);
    DECLARE_INSTANCE_FIELD(BSML::ModalView*, sizeModal);
    DECLARE_INSTANCE_FIELD(BSML::ModalView*, positionModal);
    DECLARE_INSTANCE_FIELD(BSML::ModalView*, rotationModal);

    DECLARE_INSTANCE_FIELD(StringW, editingPlacementId);

    DECLARE_INSTANCE_METHOD(void, OnNameChanged, StringW value);
    DECLARE_INSTANCE_METHOD(void, OnShowInMenuChanged, bool value);
    DECLARE_INSTANCE_METHOD(void, OnShowInGameChanged, bool value);
    DECLARE_INSTANCE_METHOD(void, OnOpenVisibilityClicked);
    DECLARE_INSTANCE_METHOD(void, OnCloseVisibilityClicked);
    DECLARE_INSTANCE_METHOD(void, OnWidthChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnHeightChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnOpenSizeClicked);
    DECLARE_INSTANCE_METHOD(void, OnCloseSizeClicked);
    DECLARE_INSTANCE_METHOD(void, OnPosXChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnPosYChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnPosZChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnOpenPositionClicked);
    DECLARE_INSTANCE_METHOD(void, OnClosePositionClicked);
    DECLARE_INSTANCE_METHOD(void, OnRotXChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnRotYChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnRotZChanged, float value);
    DECLARE_INSTANCE_METHOD(void, OnOpenRotationClicked);
    DECLARE_INSTANCE_METHOD(void, OnCloseRotationClicked);
    DECLARE_INSTANCE_METHOD(void, OnRecenterClicked);
    DECLARE_INSTANCE_METHOD(void, OnDeletePlacementClicked);
    DECLARE_INSTANCE_METHOD(void, OnCloseClicked);
};
