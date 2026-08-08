#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Imager::IO {
    //sdcard/ModData/com.beatgames.beatsaber/Mods/imager/Images/
    std::string GetImagesDirectory();

    bool EnsureImagesDirectory();

    bool IsSupportedImageUrl(std::string_view url);
    bool IsSupportedImageFile(std::string_view fileName);
    bool IsAnimatedImageUrl(std::string_view url);
    bool IsAnimatedImageFile(std::string_view fileName);

    std::vector<std::string> ListImages();

    std::string MakeUniqueImageFileName(std::string_view suggestedName, std::string_view extensionWithDot);

    std::optional<std::vector<std::uint8_t>> ReadAllBytes(std::string_view path, std::string& outError);
    bool WriteAllBytes(std::string_view path, std::span<std::uint8_t const> bytes, std::string& outError);

    bool DeleteImageFile(std::string_view fileName, std::string& outError);
}
