#pragma once

#include "HMUI/FlowCoordinator.hpp"

#include "custom-types/shared/macros.hpp"

#include "UI/ImagerLibraryViewController.hpp"
#include "UI/ImagerPlacementSettingsViewController.hpp"
#include "UI/ImagerViewController.hpp"

DECLARE_CLASS_CODEGEN(Imager::UI, ImagerFlowCoordinator, HMUI::FlowCoordinator) {
    DECLARE_SIMPLE_DTOR();

    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::FlowCoordinator::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, BackButtonWasPressed, &HMUI::FlowCoordinator::BackButtonWasPressed, HMUI::ViewController* topViewController);

    DECLARE_INSTANCE_FIELD(Imager::UI::ImagerLibraryViewController*, libraryViewController);
    DECLARE_INSTANCE_FIELD(Imager::UI::ImagerViewController*, controlsViewController);
    DECLARE_INSTANCE_FIELD(Imager::UI::ImagerPlacementSettingsViewController*, placementSettingsViewController);
};
