#include "Imager/IO.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "beatsaber-hook/shared/config/config-utils.hpp"
#include "logger.hpp"

namespace Imager::IO {
    namespace fs = std::filesystem;

    static std::string ToLower(std::string_view s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) out.push_back((char)std::tolower((unsigned char)c));
        return out;
    }

    static std::string_view Trim(std::string_view s) {
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
        return s;
    }

    static std::string_view StripQueryAndFragment(std::string_view url) {
        auto q = url.find_first_of("?#");
        if (q == std::string_view::npos) return url;
        return url.substr(0, q);
    }

    static std::string_view GetExtensionLower(std::string_view nameOrUrl) {
        auto stripped = StripQueryAndFragment(nameOrUrl);
        auto dot = stripped.find_last_of('.');
        if (dot == std::string_view::npos) return {};
        return stripped.substr(dot);
    }

    static bool HasSupportedExt(std::string_view extLowerWithDot) {
        return extLowerWithDot == ".png" || extLowerWithDot == ".jpg" || extLowerWithDot == ".jpeg" || extLowerWithDot == ".gif";
    }

    static bool HasAnimatedExt(std::string_view extLowerWithDot) {
        return extLowerWithDot == ".gif";
    }

    std::string GetImagesDirectory() {
        ///sdcard/ModData/{}/Mods/
        auto base = getDataDir(MOD_ID);
        fs::path p(base);
        p /= "Images";
        p /= "";
        return p.string();
    }

    bool EnsureImagesDirectory() {
        try {
            fs::path p(GetImagesDirectory());
            if (p.filename().empty()) p = p.parent_path();
            if (fs::exists(p)) return fs::is_directory(p);
            fs::create_directories(p);
            return true;
        } catch (std::exception const& e) {
            Logger.error(";( EnsureImagesDirectory failed: {}", e.what());
            return false;
        }
    }

    bool IsSupportedImageUrl(std::string_view url) {
        url = Trim(url);
        if (url.empty()) return false;
        auto ext = ToLower(GetExtensionLower(url));
        return HasSupportedExt(ext);
    }

    bool IsSupportedImageFile(std::string_view fileName) {
        fileName = Trim(fileName);
        if (fileName.empty()) return false;
        auto ext = ToLower(GetExtensionLower(fileName));
        return HasSupportedExt(ext);
    }

    bool IsAnimatedImageUrl(std::string_view url) {
        url = Trim(url);
        if (url.empty()) return false;
        auto ext = ToLower(GetExtensionLower(url));
        return HasAnimatedExt(ext);
    }

    bool IsAnimatedImageFile(std::string_view fileName) {
        fileName = Trim(fileName);
        if (fileName.empty()) return false;
        auto ext = ToLower(GetExtensionLower(fileName));
        return HasAnimatedExt(ext);
    }

    std::vector<std::string> ListImages() {
        std::vector<std::string> out;
        if (!EnsureImagesDirectory()) return out;

        fs::path dir(GetImagesDirectory());
        if (dir.filename().empty()) dir = dir.parent_path();

        try {
            for (auto const& entry : fs::directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                auto name = entry.path().filename().string();
                if (!IsSupportedImageFile(name)) continue;
                out.emplace_back(std::move(name));
            }
        } catch (std::exception const& e) {
            Logger.error(";( ListImages failed: {}", e.what());
            return {};
        }

        std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) {
            return ToLower(a) < ToLower(b);
        });
        return out;
    }

    static std::string SanitizeFileName(std::string_view name) {
        std::string out;
        out.reserve(name.size());
        for (char c : name) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
                out.push_back(c);
            } else {
                out.push_back('_');
            }
        }
        if (out.empty()) out = "image";
        return out;
    }

    std::string MakeUniqueImageFileName(std::string_view suggestedName, std::string_view extensionWithDot) {
        EnsureImagesDirectory();

        std::string base = SanitizeFileName(Trim(suggestedName));

        auto dot = base.find_last_of('.');
        if (dot != std::string::npos) {
            base = base.substr(0, dot);
        }

        if (base.size() > 64) base.resize(64);

        std::string extLower = ToLower(extensionWithDot);
        if (!HasSupportedExt(extLower)) extLower = ".png";

        fs::path dir(GetImagesDirectory());
        if (dir.filename().empty()) dir = dir.parent_path();

        fs::path candidate = dir / (base + extLower);
        if (!fs::exists(candidate)) return candidate.filename().string();

        for (int i = 1; i < 10'000; ++i) {
            fs::path attempt = dir / (base + "_" + std::to_string(i) + extLower);
            if (!fs::exists(attempt)) return attempt.filename().string();
        }

        return (base + "_" + std::to_string((int)std::hash<std::string>{}(std::string(suggestedName))) + extLower);
    }

    std::optional<std::vector<std::uint8_t>> ReadAllBytes(std::string_view path, std::string& outError) {
        outError.clear();
        std::ifstream f(std::string(path), std::ios::binary);
        if (!f) {
            outError = "Failed to open file";
            return std::nullopt;
        }
        f.seekg(0, std::ios::end);
        auto len = f.tellg();
        if (len < 0) {
            outError = "Failed to get file length";
            return std::nullopt;
        }
        f.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes((size_t)len);
        if (!bytes.empty()) {
            f.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)bytes.size());
        }
        if (!f && !f.eof()) {
            outError = "Failed to read file";
            return std::nullopt;
        }
        return bytes;
    }

    bool WriteAllBytes(std::string_view path, std::span<std::uint8_t const> bytes, std::string& outError) {
        outError.clear();
        std::ofstream f(std::string(path), std::ios::binary | std::ios::trunc);
        if (!f) {
            outError = "Failed to open file for writing";
            return false;
        }
        if (!bytes.empty()) {
            f.write(reinterpret_cast<char const*>(bytes.data()), (std::streamsize)bytes.size());
        }
        if (!f) {
            outError = "Failed to write file";
            return false;
        }
        return true;
    }

    bool DeleteImageFile(std::string_view fileName, std::string& outError) {
        outError.clear();
        fileName = Trim(fileName);
        if (fileName.empty()) {
            outError = "No file selected";
            return false;
        }
        if (fileName.find('/') != std::string_view::npos || fileName.find('\\') != std::string_view::npos) {
            outError = "Invalid file name";
            return false;
        }
        if (!IsSupportedImageFile(fileName)) {
            outError = "Unsupported file type";
            return false;
        }

        try {
            fs::path dir(GetImagesDirectory());
            if (dir.filename().empty()) dir = dir.parent_path();
            fs::path p = dir / std::string(fileName);
            if (!fs::exists(p)) return true;  // already gone
            return fs::remove(p);
        } catch (std::exception const& e) {
            outError = e.what();
            return false;
        }
    }
}
