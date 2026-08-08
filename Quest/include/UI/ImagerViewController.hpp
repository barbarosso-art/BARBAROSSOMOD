#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/InputFieldView.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "System/Object.hpp"
#include "UnityEngine/Transform.hpp"

#include "beatsaber-hook/shared/utils/typedefs-string.hpp"

#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/Settings/DropdownListSetting.hpp"
#include "bsml/shared/macros.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(Imager::UI, ImagerViewController, HMUI::ViewController) {
    DECLARE_SIMPLE_DTOR();

    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidDeactivate, &HMUI::ViewController::DidDeactivate, bool removedFromHierarchy, bool screenSystemDisabling);

    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, statusText);
    DECLARE_INSTANCE_FIELD(BSML::DropdownListSetting*, imageDropdown);
    DECLARE_INSTANCE_FIELD(UnityEngine::Transform*, downloadUrlKeyboardHost);

    DECLARE_INSTANCE_FIELD(HMUI::InputFieldView*, downloadUrlInput);

    DECLARE_INSTANCE_FIELD(BSML::ModalView*, downloadModal);
    DECLARE_INSTANCE_FIELD(TMPro::TextMeshProUGUI*, downloadStatusText);
    DECLARE_INSTANCE_FIELD(BSML::ModalView*, fileActionsModal);

    DECLARE_INSTANCE_FIELD(HMUI::FlowCoordinator*, flowCoordinator);

    DECLARE_BSML_PROPERTY(StringW, downloadUrl);
    DECLARE_BSML_PROPERTY(System::Object*, selectedImage);
    DECLARE_BSML_PROPERTY(bool, editMode);

    DECLARE_INSTANCE_METHOD(ListW<System::Object*>, get_imageChoices);

    DECLARE_INSTANCE_METHOD(void, OnDownloadClicked);
    DECLARE_INSTANCE_METHOD(void, OnCancelDownloadClicked);
    DECLARE_INSTANCE_METHOD(void, OnOpenFileActionsClicked);
    DECLARE_INSTANCE_METHOD(void, OnCloseFileActionsClicked);
    DECLARE_INSTANCE_METHOD(void, OnEditModeChanged, bool value);
    DECLARE_INSTANCE_METHOD(void, OnRemovePlacedForSelectedClicked);
    DECLARE_INSTANCE_METHOD(void, OnDeleteFileClicked);
    DECLARE_INSTANCE_METHOD(void, OnClearPlacedClicked);
    DECLARE_INSTANCE_METHOD(void, OnBackClicked);
};
