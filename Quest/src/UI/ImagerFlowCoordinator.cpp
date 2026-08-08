#include "UI/ImagerFlowCoordinator.hpp"

#include <exception>

#include "UI/ImagerLibraryViewController.hpp"
#include "UI/ImagerPlacementSettingsViewController.hpp"
#include "UI/ImagerViewController.hpp"

#include "bsml/shared/Helpers/creation.hpp"

#include "HMUI/ViewController.hpp"
#include "logger.hpp"

DEFINE_TYPE(Imager::UI, ImagerFlowCoordinator);

namespace Imager::UI {
    void ImagerFlowCoordinator::DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
        (void)addedToHierarchy;
        (void)screenSystemEnabling;

        try {
            this->SetTitle(StringW("Imager"), HMUI::ViewController_AnimationType::In);
            this->set_showBackButton(true);

            if (!libraryViewController) libraryViewController = BSML::Helpers::CreateViewController<ImagerLibraryViewController*>();
            if (!controlsViewController) controlsViewController = BSML::Helpers::CreateViewController<ImagerViewController*>();
            if (!placementSettingsViewController) placementSettingsViewController = BSML::Helpers::CreateViewController<ImagerPlacementSettingsViewController*>();

            if (controlsViewController) controlsViewController->flowCoordinator = this;

            if (firstActivation) {
                this->ProvideInitialViewControllers(
                    libraryViewController,
                    controlsViewController,
                    placementSettingsViewController,
                    nullptr,
                    nullptr
                );
            }
        } catch (const std::exception& e) {
            Logger.error(";( ImagerFlowCoordinator::DidActivate failed: {}", e.what());
        } catch (...) {
            Logger.error(";( ImagerFlowCoordinator::DidActivate failed: unknown");
        }
    }

    void ImagerFlowCoordinator::BackButtonWasPressed(HMUI::ViewController* topViewController) {
        (void)topViewController;

        auto parent = this->_parentFlowCoordinator;
        if (parent) {
            parent.ptr()->DismissFlowCoordinator(this, HMUI::ViewController_AnimationDirection::Horizontal, nullptr, false);
        }
    }
}
