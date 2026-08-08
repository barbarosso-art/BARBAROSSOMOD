#include "GlobalNamespace/MainFlowCoordinator.hpp"

#include "Imager/RuntimeManager.hpp"
#include "autohooks/shared/hooks.hpp"
#include "logger.hpp"

using namespace GlobalNamespace;

MAKE_LATE_HOOK_MATCH(
    MainFlowCoordinator_DidActivate,
    &MainFlowCoordinator::DidActivate,
    void,
    MainFlowCoordinator* self,
    bool firstActivation,
    bool addedToHierarchy,
    bool screenSystemEnabling
) {
    MainFlowCoordinator_DidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);

    if (firstActivation) {
        Logger.info("Main menu activated");
        Imager::EnsureRuntimeManager();
    }
}
