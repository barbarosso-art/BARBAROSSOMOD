#include "UI/LibraryState.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "Imager/IO.hpp"
#include "logger.hpp"

#include "beatsaber-hook/shared/utils/typedefs.h"

#include "bsml/shared/BSML-Lite.hpp"

#include "UnityEngine/ImageConversion.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/Texture2D.hpp"

namespace Imager::UI::LibraryState {
    using namespace UnityEngine;

    struct Thumb {
        Texture2D* texture = nullptr;
        Sprite* sprite = nullptr;
    };

    static std::vector<std::string> g_images;
    static std::unordered_map<std::string, Thumb> g_thumbs;

    static Texture2D* LoadTextureFromFile(std::string_view fileName, std::string& outError) {
        outError.clear();

        auto fullPath = Imager::IO::GetImagesDirectory() + std::string(fileName);
        auto bytesOpt = Imager::IO::ReadAllBytes(fullPath, outError);
        if (!bytesOpt) return nullptr;

        ArrayW<std::uint8_t> data(*bytesOpt);
        auto* tex = Texture2D::New_ctor(2, 2);
        if (!tex) {
            outError = "Failed to create Texture2D";
            return nullptr;
        }
        if (!ImageConversion::LoadImage(tex, data, true)) {
            outError = "Failed to decode image";
            Object::Destroy(tex);
            return nullptr;
        }
        return tex;
    }

    static void DestroyThumb(Thumb& t) {
        if (t.sprite) Object::Destroy(t.sprite);
        if (t.texture) Object::Destroy(t.texture);
        t.sprite = nullptr;
        t.texture = nullptr;
    }

    void Refresh() {
        g_images = Imager::IO::ListImages();

        for (auto it = g_thumbs.begin(); it != g_thumbs.end();) {
            bool exists = std::find(g_images.begin(), g_images.end(), it->first) != g_images.end();
            if (!exists) {
                DestroyThumb(it->second);
                it = g_thumbs.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<std::string> const& GetImages() {
        return g_images;
    }

    UnityEngine::Sprite* GetOrCreateThumbnail(std::string_view fileName) {
        auto key = std::string(fileName);
        auto it = g_thumbs.find(key);
        if (it != g_thumbs.end()) {
            if (it->second.sprite) return it->second.sprite;
        }

        std::string err;
        auto* tex = LoadTextureFromFile(fileName, err);
        if (!tex) {
            Logger.warn("Thumbnail load failed '{}': {}", fileName, err);
            return nullptr;
        }

        auto* sprite = BSML::Lite::TextureToSprite(tex);
        if (!sprite) {
            Logger.warn("Thumbnail sprite creation failed '{}'", fileName);
            Object::Destroy(tex);
            return nullptr;
        }

        g_thumbs[key] = Thumb{tex, sprite};
        return sprite;
    }

    void ClearThumbnailCache() {
        for (auto& [k, v] : g_thumbs) {
            (void)k;
            DestroyThumb(v);
        }
        g_thumbs.clear();
    }
}
