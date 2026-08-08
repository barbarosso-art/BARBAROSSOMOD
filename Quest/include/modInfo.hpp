#pragma once

#include "scotland2/shared/loader.hpp"

/// @brief Stable public ID and semantic version sent to the modloader.
/// Git state must not change the version declared by the QMOD manifest.
inline modloader::ModInfo const modInfo{MOD_ID, VERSION, 0};
