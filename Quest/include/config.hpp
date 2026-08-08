#pragma once

#include <string>
#include <vector>

#include "config-utils/shared/config-utils.hpp"

namespace Imager {
    DECLARE_JSON_STRUCT(Placement) {
        VALUE(std::string, Id);
        VALUE(std::string, FileName);
        VALUE_DEFAULT(std::string, Name, "");

        VALUE_DEFAULT(ConfigUtils::Vector3, Position, ConfigUtils::Vector3(0.0f, 0.0f, 0.0f));
        VALUE_DEFAULT(ConfigUtils::Vector3, Rotation, ConfigUtils::Vector3(0.0f, 0.0f, 0.0f));
        VALUE_DEFAULT(ConfigUtils::Vector3, Scale, ConfigUtils::Vector3(1.0f, 1.0f, 1.0f));

        VALUE_DEFAULT(ConfigUtils::Vector3, HomePosition, ConfigUtils::Vector3(0.0f, 0.0f, 0.0f));
        VALUE_DEFAULT(ConfigUtils::Vector3, HomeRotation, ConfigUtils::Vector3(0.0f, 0.0f, 0.0f));

        VALUE_DEFAULT(bool, ShowInMenu, true);
        VALUE_DEFAULT(bool, ShowInMap, true);
    };

    DECLARE_CONFIG(ModConfig) {
        CONFIG_VALUE(Enabled, bool, "Enabled", true);
        CONFIG_VALUE(Placements, std::vector<Placement>, "Placements", {});
    };
}
