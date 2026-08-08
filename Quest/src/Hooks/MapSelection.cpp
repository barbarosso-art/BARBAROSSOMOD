#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"

#include "Imager/RuntimeManager.hpp"
#include "autohooks/shared/hooks.hpp"
#include "logger.hpp"

using namespace GlobalNamespace;

MAKE_LATE_HOOK_MATCH(
    StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes,
    &StandardLevelScenesTransitionSetupDataSO::InitAndSetupScenes,
    void,
    StandardLevelScenesTransitionSetupDataSO* self,
    PlayerSpecificSettings* playerSpecificSettings,
    StringW backButtonText,
    bool startPaused
) {
    auto* level = self ? self->get_beatmapLevel() : nullptr;
    if (level) {
        Imager::LoadMapPlacementForSong((std::string)level->songName);
    } else {
        Imager::LoadMapPlacementForSong({});
    }
    StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes(
        self, playerSpecificSettings, backButtonText, startPaused
    );
}
