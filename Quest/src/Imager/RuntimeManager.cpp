#include "Imager/RuntimeManager.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config.hpp"
#include "logger.hpp"

#include "rapidjson/document.h"

#include "Imager/IO.hpp"
#include "Imager/PlacedImage.hpp"
#include "UI/AppEvents.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "bsml/shared/BSML/Animations/AnimationLoader.hpp"
#include "bsml/shared/BSML/Animations/AnimationController.hpp"
#include "bsml/shared/BSML/Animations/AnimationControllerData.hpp"
#include "bsml/shared/BSML/Animations/AnimationStateUpdater.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "bsml/shared/BSML/Components/ClickableImage.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"

#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Color32.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/ImageConversion.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/RawImage.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"

DEFINE_TYPE(Imager, RuntimeManager);
DEFINE_TYPE(Imager, PlacedImage);

namespace Imager {
    using namespace UnityEngine;

    static constexpr std::string_view kMapPlacementId = "__map_owned_background__";
    static constexpr std::string_view kCustomLevelsRoot =
        "/sdcard/ModData/com.beatgames.beatsaber/Mods/SongCore/CustomLevels";

    static Vector3 Add(Vector3 a, Vector3 b) {
        return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    static Vector3 Mul(Vector3 v, float s) {
        return Vector3(v.x * s, v.y * s, v.z * s);
    }

    static Vector3 Neg(Vector3 v) {
        return Vector3(-v.x, -v.y, -v.z);
    }

    static float Length(Vector3 v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    static Vector3 Normalize(Vector3 v) {
        float len = Length(v);
        if (len <= 0.00001f) return Vector3(0.0f, 0.0f, 0.0f);
        return Vector3(v.x / len, v.y / len, v.z / len);
    }

    static float DistanceSq(Vector3 a, Vector3 b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    static SafePtrUnity<RuntimeManager> g_manager;
    static bool g_spawnedSaved = false;
    static bool g_editMode = false;

    static std::unordered_map<std::string, SafePtrUnity<BSML::FloatingScreen>> g_spawned;
    struct GifAttachTarget {
        SafePtrUnity<UnityEngine::UI::Image> image;
        bool startPlaying = true;
    };
    static std::unordered_map<std::string, std::vector<GifAttachTarget>> g_pendingGifAttaches;
    static std::deque<std::pair<std::string, std::string>> g_gifDecodeQueue;  // key, fileName
    static std::unordered_set<std::string> g_gifDecodeQueuedOrRunning;
    static bool g_gifDecodeRunning = false;

    static std::string g_lastSceneName;
    static bool g_lastMapContext = false;

    struct GrabInfo {
        float startTime = 0.0f;
        Vector3 startPos{};
        Quaternion startRot{};
    };
    static std::unordered_map<std::string, GrabInfo> g_grabInfo;

    static void ApplySceneVisibility();
    static void ResizePlacement(std::string const& placementId, float factor);
    static bool SetPlacementShowInMenu(std::string_view placementId, bool value, bool save);
    static bool SetPlacementShowInMap(std::string_view placementId, bool value, bool save);

    static std::string NewPlacementId() {
        static std::atomic<int> counter{0};
        auto t = Time::get_realtimeSinceStartup();
        return std::to_string((int64_t)(t * 1000.0f)) + "_" + std::to_string(counter.fetch_add(1));
    }

    static Texture2D* LoadTextureFromFile(std::string_view fileName, std::string& outError) {
        outError.clear();

        auto requested = std::filesystem::path(std::string(fileName));
        auto fullPath = requested.is_absolute() ? requested.string() : IO::GetImagesDirectory() + std::string(fileName);
        auto bytesOpt = IO::ReadAllBytes(fullPath, outError);
        if (!bytesOpt) return nullptr;

        ArrayW<std::uint8_t> data(*bytesOpt);
        auto* tex = Texture2D::New_ctor(2, 2);
        if (!tex) {
            outError = "Failed to create Texture2D";
            return nullptr;
        }
        if (!ImageConversion::LoadImage(tex, data, true)) {
            outError = "Failed to decode image";
            return nullptr;
        }
        return tex;
    }

    static bool TryGetGifDimensions(std::span<std::uint8_t const> bytes, float& outWidth, float& outHeight) {
        if (bytes.size() < 10) return false;

        bool gif87 = bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8' && bytes[4] == '7' && bytes[5] == 'a';
        bool gif89 = bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8' && bytes[4] == '9' && bytes[5] == 'a';
        if (!gif87 && !gif89) return false;

        std::uint16_t width = (std::uint16_t)bytes[6] | ((std::uint16_t)bytes[7] << 8);
        std::uint16_t height = (std::uint16_t)bytes[8] | ((std::uint16_t)bytes[9] << 8);
        if (width == 0 || height == 0) return false;

        outWidth = (float)width;
        outHeight = (float)height;
        return true;
    }

    static void ApplyGifDataToImage(UnityEngine::UI::Image* image, BSML::AnimationControllerData* data, bool startPlaying) {
        if (!image || !data) return;

        auto* updater = image->GetComponent<BSML::AnimationStateUpdater*>();
        if (!updater) updater = image->get_gameObject()->AddComponent<BSML::AnimationStateUpdater*>();
        if (!updater) return;

        updater->image = image;
        updater->set_controllerData(data);
        updater->set_enabled(true);
        data->set_isPlaying(startPlaying);
    }

    static void FinalizeGifDecodeJob(std::string const& key, BSML::AnimationControllerData* dataOrNull) {
        auto waitersIt = g_pendingGifAttaches.find(key);
        if (waitersIt != g_pendingGifAttaches.end()) {
            for (auto const& target : waitersIt->second) {
                if (!target.image) continue;
                auto* image = target.image.ptr();
                if (!image || !image->m_CachedPtr.m_value) continue;
                if (dataOrNull) ApplyGifDataToImage(image, dataOrNull, target.startPlaying);
            }
            g_pendingGifAttaches.erase(waitersIt);
        }

        g_gifDecodeQueuedOrRunning.erase(key);
        g_gifDecodeRunning = false;
    }

    static void StartNextGifDecodeJob() {
        if (g_gifDecodeRunning) return;
        if (g_gifDecodeQueue.empty()) return;

        auto [key, fileName] = g_gifDecodeQueue.front();
        g_gifDecodeQueue.pop_front();
        g_gifDecodeRunning = true;

        std::string ioErr;
        auto bytesOpt = IO::ReadAllBytes(IO::GetImagesDirectory() + fileName, ioErr);
        if (!bytesOpt || bytesOpt->empty()) {
            Logger.warn(";( Failed to read GIF '{}': {}", fileName, ioErr.empty() ? "empty file" : ioErr);
            FinalizeGifDecodeJob(key, nullptr);
            StartNextGifDecodeJob();
            return;
        }

        static constexpr size_t kMaxGifBytes = 15 * 1024 * 1024;
        if (bytesOpt->size() > kMaxGifBytes) {
            Logger.warn("GIF '{}' too large ({} bytes, max 15MB), skipping", fileName, bytesOpt->size());
            FinalizeGifDecodeJob(key, nullptr);
            StartNextGifDecodeJob();
            return;
        }

        float gifW = 0.0f;
        float gifH = 0.0f;
        if (TryGetGifDimensions(std::span<std::uint8_t const>(bytesOpt->data(), bytesOpt->size()), gifW, gifH)) {
            if (gifW > 2048.0f || gifH > 2048.0f) {
                Logger.warn("GIF '{}' too large ({}x{}), skipping", fileName, gifW, gifH);
                FinalizeGifDecodeJob(key, nullptr);
                StartNextGifDecodeJob();
                return;
            }
        }

        ArrayW<std::uint8_t> data(*bytesOpt);
        BSML::AnimationLoader::Process(
            BSML::AnimationLoader::AnimationType::GIF,
            data,
            [key, fileName](UnityEngine::Texture2D* tex, ArrayW<UnityEngine::Rect> uvs, ArrayW<float> delays) {
                BSML::MainThreadScheduler::Schedule([key, fileName, tex, uvs, delays]() {
                    auto* controller = BSML::AnimationController::get_instance();
                    BSML::AnimationControllerData* dataObj = nullptr;
                    if (controller) {
                        if (!controller->TryGetAnimationControllerData(StringW(key), dataObj)) {
                            dataObj = controller->Register(StringW(key), tex, uvs, delays);
                        } else {
                            if (tex && tex->m_CachedPtr.m_value) UnityEngine::Object::Destroy(tex);
                        }
                    } else {
                        if (tex && tex->m_CachedPtr.m_value) UnityEngine::Object::Destroy(tex);
                    }

                    if (!dataObj) {
                        Logger.warn(";( Failed to register GIF animation '{}'", fileName);
                    }

                    FinalizeGifDecodeJob(key, dataObj);
                    StartNextGifDecodeJob();
                });
            },
            [key, fileName]() {
                BSML::MainThreadScheduler::Schedule([key, fileName]() {
                    Logger.warn(";( Failed to decode GIF '{}'", fileName);
                    FinalizeGifDecodeJob(key, nullptr);
                    StartNextGifDecodeJob();
                });
            }
        );
    }

    static void AttachGifAnimationFromFile(UnityEngine::UI::Image* image, std::string const& fileName, bool startPlaying, std::string_view keyPrefix) {
        if (!image) return;

        auto* updater = image->GetComponent<BSML::AnimationStateUpdater*>();
        if (!updater) updater = image->get_gameObject()->AddComponent<BSML::AnimationStateUpdater*>();
        if (!updater) return;

        updater->image = image;
        updater->set_controllerData(nullptr);
        updater->set_enabled(false);

        std::string key = std::string(keyPrefix) + "_" + fileName;
        auto* controller = BSML::AnimationController::get_instance();
        if (controller) {
            BSML::AnimationControllerData* existing = nullptr;
            if (controller->TryGetAnimationControllerData(StringW(key), existing) && existing) {
                ApplyGifDataToImage(image, existing, startPlaying);
                return;
            }
        }

        auto& targets = g_pendingGifAttaches[key];
        targets.push_back(GifAttachTarget{SafePtrUnity<UnityEngine::UI::Image>(image), startPlaying});

        if (!g_gifDecodeQueuedOrRunning.insert(key).second) return;
        g_gifDecodeQueue.emplace_back(key, fileName);
        StartNextGifDecodeJob();
    }

    static std::optional<std::string> TryGetPlacementId(BSML::FloatingScreen* screen) {
        if (!screen) return std::nullopt;
        auto goW = screen->get_gameObject();
        auto* go = goW.ptr();
        if (!go) return std::nullopt;

        auto* placed = go->GetComponent<PlacedImage*>();
        if (!placed || !placed->placementId) return std::nullopt;

        return (std::string)placed->placementId;
    }

    static bool UpdatePlacementFromScreen(std::string_view placementId, BSML::FloatingScreen* screen, bool save) {
        if (!screen) return false;

        auto placements = getModConfig().Placements.GetValue();
        bool found = false;

        for (auto& p : placements) {
            if (p.Id == placementId) {
                p.Position = screen->get_ScreenPosition();
                p.Rotation = screen->get_ScreenRotation().get_eulerAngles();

                auto size = screen->get_ScreenSize();
                p.Scale = Vector3(size.x, size.y, 1.0f);
                found = true;
                break;
            }
        }

        if (found) getModConfig().Placements.SetValue(placements, save);
        return found;
    }

    static bool IsDefaultScale(Placement const& placement) {
        return placement.Scale.x == 1.0f && placement.Scale.y == 1.0f && placement.Scale.z == 1.0f;
    }

    static Vector2 ComputeScreenSize(Placement const& placement, Texture2D* tex, std::optional<float> aspectOverride, bool& didAutoSize, bool& didMigrate) {
        didAutoSize = false;
        didMigrate = false;

        if (IsDefaultScale(placement)) {
            float aspect = 1.0f;
            bool hasAspect = false;

            if (aspectOverride && *aspectOverride > 0.0001f) {
                aspect = *aspectOverride;
                hasAspect = true;
            } else if (tex) {
                auto w = (float)tex->get_width();
                auto h = (float)tex->get_height();
                if (h > 0.0f) {
                    aspect = (w / h);
                    hasAspect = true;
                }
            }

            if (hasAspect) {
                float baseHeight = 50.0f;
                float baseWidth = baseHeight * aspect;
                didAutoSize = true;
                return Vector2(baseWidth, baseHeight);
            }
        }

        float sx = placement.Scale.x;
        float sy = placement.Scale.y;

        if (sx <= 10.0f && sy <= 10.0f && !IsDefaultScale(placement)) {
            sx *= 100.0f;
            sy *= 100.0f;
            didMigrate = true;
        }

        if (sx < 10.0f) sx = 10.0f;
        if (sy < 10.0f) sy = 10.0f;

        return Vector2(sx, sy);
    }

    static void OnFloatingScreenHandleGrabbed(BSML::FloatingScreen* screen, BSML::FloatingScreenHandleEventArgs const& args) {
        auto pid = TryGetPlacementId(screen);
        if (!pid) return;

        GrabInfo info;
        info.startTime = Time::get_realtimeSinceStartup();
        info.startPos = args.position;
        info.startRot = args.rotation;
        g_grabInfo[*pid] = info;
    }

    static void OnFloatingScreenHandleReleased(BSML::FloatingScreen* screen, BSML::FloatingScreenHandleEventArgs const& args) {
        auto pid = TryGetPlacementId(screen);
        if (!pid) return;

        UpdatePlacementFromScreen(*pid, screen, true);

        auto it = g_grabInfo.find(*pid);
        if (it == g_grabInfo.end()) return;
        auto info = it->second;
        g_grabInfo.erase(it);

        if (!g_editMode) return;

        float dt = Time::get_realtimeSinceStartup() - info.startTime;
        float moveSq = DistanceSq(args.position, info.startPos);
        if (dt < 0.45f && moveSq < 0.0004f) {  // short tap
            // open the placement ui
            Imager::UI::NotifyEditPlacementRequested(*pid);
        }
    }

    static void ConfigureHandle(BSML::FloatingScreen* screen, Vector2 screenSize) {
        if (!screen) return;

        screen->set_ShowHandle(g_editMode);
        screen->set_HighlightHandle(false);
        screen->set_HandleSide(BSML::Side::Full);
        screen->UpdateHandle();

        if (!screen->handle) {
            Logger.warn("FloatingScreen created without a handle");
            return;
        }

        auto* handleGo = screen->handle;
        if (handleGo) {
            if (auto renderers = handleGo->GetComponentsInChildren<UnityEngine::MeshRenderer*>()) {
                for (auto* r : renderers) {
                    if (!r) continue;
                    r->set_enabled(false);
                }
            }
        }

        auto htW = screen->handle->get_transform();
        auto* ht = htW.ptr();
        if (!ht) return;

        ht->set_localPosition(Vector3(0.0f, 0.0f, -2.0f));

        float sx = std::max(4.0f, screenSize.x * 0.95f);
        float sy = std::max(3.0f, screenSize.y * 0.95f);
        ht->set_localScale(Vector3(sx, sy, 1.0f));
    }

    static void ApplyEditModeToScreen(BSML::FloatingScreen* screen, bool enabled) {
        if (!screen) return;

        screen->set_ShowHandle(enabled);
        screen->set_HighlightHandle(false);

        auto goW = screen->get_gameObject();
        auto* go = goW.ptr();
        if (!go) return;

        if (auto* img = go->GetComponentInChildren<BSML::ClickableImage*>()) {
            img->set_raycastTarget(false);
        }
    }

    static void ResizePlacement(std::string const& placementId, float factor) {
        auto it = g_spawned.find(placementId);
        if (it == g_spawned.end()) return;
        if (!it->second) return;

        auto* screen = it->second.ptr();
        auto size = screen->get_ScreenSize();
        size.x *= factor;
        size.y *= factor;

        if (size.x < 10.0f) size.x = 10.0f;
        if (size.y < 10.0f) size.y = 10.0f;
        if (size.x > 400.0f) size.x = 400.0f;
        if (size.y > 400.0f) size.y = 400.0f;

        screen->set_ScreenSize(size);
        ConfigureHandle(screen, size);
        UpdatePlacementFromScreen(placementId, screen, true);
    }

    static bool IsZeroVec3(ConfigUtils::Vector3 const& v) {
        return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
    }

    static std::optional<Placement> GetPlacementCopy(std::string_view placementId) {
        auto placements = getModConfig().Placements.GetValue();
        for (auto const& p : placements) {
            if (p.Id == placementId) return p;
        }
        return std::nullopt;
    }

    static bool SetPlacementShowInMenu(std::string_view placementId, bool value, bool save) {
        auto placements = getModConfig().Placements.GetValue();
        for (auto& p : placements) {
            if (p.Id != placementId) continue;
            p.ShowInMenu = value;
            getModConfig().Placements.SetValue(placements, save);
            return true;
        }
        return false;
    }

    static bool SetPlacementShowInMap(std::string_view placementId, bool value, bool save) {
        auto placements = getModConfig().Placements.GetValue();
        for (auto& p : placements) {
            if (p.Id != placementId) continue;
            p.ShowInMap = value;
            getModConfig().Placements.SetValue(placements, save);
            return true;
        }
        return false;
    }

    static bool IsMapSceneName(std::string const& name) {
        return name.find("GameCore") != std::string::npos || name.find("MultiplayerCore") != std::string::npos || name.find("MissionLevel") != std::string::npos;
    }

    static bool IsMenuSceneName(std::string const& name) {
        return name.find("Menu") != std::string::npos;
    }

    static std::string GetActiveSceneName() {
        auto scene = UnityEngine::SceneManagement::SceneManager::GetActiveScene();
        if (!scene.IsValid()) return {};
        return (std::string)scene.get_name();
    }

    static bool IsMapSceneLoaded() {
        auto count = UnityEngine::SceneManagement::SceneManager::get_sceneCount();
        for (int i = 0; i < count; ++i) {
            auto scene = UnityEngine::SceneManagement::SceneManager::GetSceneAt(i);
            if (!scene.IsValid()) continue;
            if (!scene.get_isLoaded()) continue;
            auto name = (std::string)scene.get_name();
            if (IsMapSceneName(name)) return true;
        }
        return false;
    }

    static bool ComputeMapContext() {
        auto activeSceneName = GetActiveSceneName();
        if (!activeSceneName.empty()) {
            if (IsMenuSceneName(activeSceneName)) return false;
            if (IsMapSceneName(activeSceneName)) return true;
        }

        return IsMapSceneLoaded();
    }

    static void ApplySceneVisibility() {
        auto placements = getModConfig().Placements.GetValue();
        std::unordered_map<std::string, Placement> byId;
        byId.reserve(placements.size());
        for (auto const& p : placements) byId.emplace(p.Id, p);

        bool isMap = ComputeMapContext();

        for (auto& [placementId, screen] : g_spawned) {
            if (!screen) continue;
            auto* fs = screen.ptr();
            if (!fs) continue;
            auto goW = fs->get_gameObject();
            auto* go = goW.ptr();
            if (!go) continue;

            bool visible = true;
            if (!g_editMode) {
                if (placementId == kMapPlacementId) {
                    visible = isMap && getModConfig().Enabled.GetValue();
                } else if (auto it = byId.find(placementId); it != byId.end()) {
                    visible = isMap ? it->second.ShowInMap : it->second.ShowInMenu;
                }
            }
            go->SetActive(visible);
        }
    }

    static void RecenterPlacement(std::string const& placementId) {
        auto it = g_spawned.find(placementId);
        if (it == g_spawned.end() || !it->second) return;

        auto placementOpt = GetPlacementCopy(placementId);
        if (!placementOpt) return;
        auto placement = *placementOpt;

        Vector3 homePos = placement.HomePosition;
        Vector3 homeRot = placement.HomeRotation;

        if (IsZeroVec3(placement.HomePosition) && !IsZeroVec3(placement.Position)) homePos = placement.Position;
        if (IsZeroVec3(placement.HomeRotation) && !IsZeroVec3(placement.Rotation)) homeRot = placement.Rotation;

        auto* screen = it->second.ptr();
        screen->set_ScreenPosition(homePos);
        screen->set_ScreenRotation(Quaternion::Euler(homeRot));

        UpdatePlacementFromScreen(placementId, screen, true);
    }

    static SafePtrUnity<BSML::FloatingScreen> SpawnPlacementObject(Placement const& placement, bool autoSizeIfDefaultScale) {
        (void)autoSizeIfDefaultScale;
        if (placement.Id.empty()) return nullptr;

        if (g_spawned.contains(placement.Id)) return g_spawned.at(placement.Id);

        if (!IO::IsSupportedImageFile(placement.FileName)) {
            Logger.warn("Unsupported file '{}'", placement.FileName);
            return nullptr;
        }

        bool isAnimated = IO::IsAnimatedImageFile(placement.FileName);
        std::string err;
        Texture2D* tex = nullptr;
        std::optional<float> aspectOverride{};

        if (isAnimated) {
            auto bytesOpt = IO::ReadAllBytes(IO::GetImagesDirectory() + placement.FileName, err);
            if (!bytesOpt) {
                Logger.warn(";( Failed to load animated image '{}': {}", placement.FileName, err);
                return nullptr;
            }

            float gifW = 0.0f;
            float gifH = 0.0f;
            if (TryGetGifDimensions(std::span<std::uint8_t const>(bytesOpt->data(), bytesOpt->size()), gifW, gifH) && gifH > 0.0f) {
                aspectOverride = gifW / gifH;
            }
        } else {
            tex = LoadTextureFromFile(placement.FileName, err);
            if (!tex) {
                Logger.warn(";( Failed to load texture '{}': {}", placement.FileName, err);
                return nullptr;
            }
        }

        bool didAutoSize = false;
        bool didMigrate = false;
        auto screenSize = ComputeScreenSize(placement, tex, aspectOverride, didAutoSize, didMigrate);

        auto* screen = BSML::Lite::CreateFloatingScreen(screenSize, placement.Position, placement.Rotation, 0.0f, false, true, BSML::Side::Bottom);
        if (!screen) {
            Logger.error(";( Failed to create FloatingScreen {}", placement.Id);
            return nullptr;
        }

        screen->set_ScreenSize(screenSize);
        screen->set_ScreenPosition(placement.Position);
        screen->set_ScreenRotation(Quaternion::Euler(placement.Rotation));

        ConfigureHandle(screen, screenSize);

        auto screenGoW = screen->get_gameObject();
        auto* screenGo = screenGoW.ptr();
        if (!screenGo) return nullptr;
        screenGo->set_name(StringW("ImagerImage"));

        auto* placed = screenGo->AddComponent<PlacedImage*>();
        placed->placementId = StringW(placement.Id);
        placed->fileName = StringW(placement.FileName);

        auto* rootRt = screen->get_rectTransform();
        if (rootRt) {
            UnityEngine::Sprite* sprite = nullptr;
            if (isAnimated) {
                sprite = BSML::Utilities::ImageResources::GetWhitePixel();
            } else {
                sprite = BSML::Lite::TextureToSprite(tex);
            }

            auto* img = BSML::Lite::CreateClickableImage(rootRt, sprite, nullptr);
            if (img) {
                img->set_preserveAspect(true);
                img->set_raycastTarget(false);
                auto rtW = img->get_rectTransform();
                auto* rt = rtW.ptr();
                if (rt) {
                    rt->set_anchorMin(Vector2(0.0f, 0.0f));
                    rt->set_anchorMax(Vector2(1.0f, 1.0f));
                    rt->set_anchoredPosition(Vector2(0.0f, 0.0f));
                    rt->set_sizeDelta(Vector2(0.0f, 0.0f));
                }

                if (isAnimated) {
                    AttachGifAnimationFromFile(reinterpret_cast<UnityEngine::UI::Image*>(img), placement.FileName, true, "imager_spawn");
                }
            }
        }

        if (auto* canvas = screenGo->GetComponent<Canvas*>()) {
            canvas->set_overrideSorting(true);
            canvas->set_sortingOrder(1000);
        } else if (auto* canvas2 = screenGo->GetComponentInChildren<Canvas*>()) {
            canvas2->set_overrideSorting(true);
            canvas2->set_sortingOrder(1000);
        }

        Object::DontDestroyOnLoad(screenGo);

        screen->HandleReleased.addCallback(&OnFloatingScreenHandleReleased);
        screen->HandleGrabbed.addCallback(&OnFloatingScreenHandleGrabbed);

        g_spawned.emplace(placement.Id, SafePtrUnity<BSML::FloatingScreen>(screen));

        if (didAutoSize || didMigrate) {
            UpdatePlacementFromScreen(placement.Id, screen, true);
        }

        return g_spawned.at(placement.Id);
    }

    void SetEditMode(bool enabled) {
        bool changed = (g_editMode != enabled);
        g_editMode = enabled;
        if (changed) Logger.info("Changed mode to edit: {}", enabled ? "ON" : "OFF");

        for (auto& [placementId, screen] : g_spawned) {
            (void)placementId;
            if (!screen) continue;
            ApplyEditModeToScreen(screen.ptr(), enabled);
        }

        ApplySceneVisibility();
    }

    bool GetEditMode() {
        return g_editMode;
    }

    static bool RemovePlacementFromConfig(std::string_view placementId) {
        auto placements = getModConfig().Placements.GetValue();
        auto oldSize = placements.size();

        placements.erase(
            std::remove_if(placements.begin(), placements.end(), [&](auto const& p) { return p.Id == placementId; }),
            placements.end()
        );

        if (placements.size() == oldSize) return false;
        getModConfig().Placements.SetValue(placements, true);
        return true;
    }

    void RemovePlacement(std::string_view placementId) {
        auto it = g_spawned.find(std::string(placementId));
        if (it != g_spawned.end()) {
            if (it->second) {
                auto goW = it->second.ptr()->get_gameObject();
                auto* go = goW.ptr();
                if (go) Object::Destroy(go);
            }
            g_spawned.erase(it);
        }

        RemovePlacementFromConfig(placementId);
        ApplySceneVisibility();
    }

    void ClearAllPlacements() {
        for (auto& [placementId, screen] : g_spawned) {
            (void)placementId;
            if (!screen) continue;
            auto goW = screen.ptr()->get_gameObject();
            auto* go = goW.ptr();
            if (go) Object::Destroy(go);
        }
        g_spawned.clear();

        getModConfig().Placements.SetValue({}, true);
    }

    void RemovePlacementsForFile(std::string_view fileName) {
        if (fileName.empty()) return;

        auto placements = getModConfig().Placements.GetValue();
        bool anyRemoved = false;

        for (auto const& p : placements) {
            if (p.FileName != fileName) continue;
            anyRemoved = true;

            auto it = g_spawned.find(p.Id);
            if (it != g_spawned.end()) {
                if (it->second) {
                    auto goW = it->second.ptr()->get_gameObject();
                    auto* go = goW.ptr();
                    if (go) Object::Destroy(go);
                }
                g_spawned.erase(it);
            }
        }

        if (!anyRemoved) return;

        placements.erase(
            std::remove_if(placements.begin(), placements.end(), [&](auto const& p) { return p.FileName == fileName; }),
            placements.end()
        );
        getModConfig().Placements.SetValue(placements, true);
        ApplySceneVisibility();
    }

    static bool SetPlacementNameInternal(std::string_view placementId, std::string_view name, bool save) {
        auto placements = getModConfig().Placements.GetValue();
        for (auto& p : placements) {
            if (p.Id != placementId) continue;
            p.Name = std::string(name);
            getModConfig().Placements.SetValue(placements, save);
            return true;
        }
        return false;
    }

    bool TryGetPlacementInfo(std::string_view placementId, PlacementInfo& outInfo) {
        outInfo = PlacementInfo{};
        if (placementId.empty()) return false;

        auto placementOpt = GetPlacementCopy(placementId);
        if (!placementOpt) return false;
        auto const& p = *placementOpt;

        outInfo.id = p.Id;
        outInfo.fileName = p.FileName;
        outInfo.name = p.Name;
        outInfo.showInMenu = p.ShowInMenu;
        outInfo.showInGame = p.ShowInMap;

        Vector3 pos = p.Position;
        Vector3 rot = p.Rotation;
        Vector2 size = Vector2(p.Scale.x, p.Scale.y);

        auto it = g_spawned.find(p.Id);
        if (it != g_spawned.end() && it->second) {
            auto* screen = it->second.ptr();
            pos = screen->get_ScreenPosition();
            rot = screen->get_ScreenRotation().get_eulerAngles();
            size = screen->get_ScreenSize();
        } else {
            if (size.x <= 1.0f && size.y <= 1.0f) size = Vector2(50.0f, 50.0f);
        }

        outInfo.posX = pos.x;
        outInfo.posY = pos.y;
        outInfo.posZ = pos.z;

        outInfo.rotX = rot.x;
        outInfo.rotY = rot.y;
        outInfo.rotZ = rot.z;

        outInfo.width = size.x;
        outInfo.height = size.y;

        return true;
    }

    bool SetPlacementName(std::string_view placementId, std::string_view name) {
        if (placementId.empty()) return false;
        return SetPlacementNameInternal(placementId, name, true);
    }

    bool SetPlacementShowInMenu(std::string_view placementId, bool value) {
        if (placementId.empty()) return false;
        bool ok = SetPlacementShowInMenu(placementId, value, true);
        if (ok) ApplySceneVisibility();
        return ok;
    }

    bool SetPlacementShowInGame(std::string_view placementId, bool value) {
        if (placementId.empty()) return false;
        bool ok = SetPlacementShowInMap(placementId, value, true);
        if (ok) ApplySceneVisibility();
        return ok;
    }

    static bool SetPlacementPositionInConfig(std::string_view placementId, Vector3 pos, bool save) {
        auto placements = getModConfig().Placements.GetValue();
        for (auto& p : placements) {
            if (p.Id != placementId) continue;
            p.Position = pos;
            getModConfig().Placements.SetValue(placements, save);
            return true;
        }
        return false;
    }

    static bool SetPlacementRotationInConfig(std::string_view placementId, Vector3 rotEuler, bool save) {
        auto placements = getModConfig().Placements.GetValue();
        for (auto& p : placements) {
            if (p.Id != placementId) continue;
            p.Rotation = rotEuler;
            getModConfig().Placements.SetValue(placements, save);
            return true;
        }
        return false;
    }

    static bool SetPlacementSizeInConfig(std::string_view placementId, Vector2 size, bool save) {
        auto placements = getModConfig().Placements.GetValue();
        for (auto& p : placements) {
            if (p.Id != placementId) continue;
            p.Scale = Vector3(size.x, size.y, 1.0f);
            getModConfig().Placements.SetValue(placements, save);
            return true;
        }
        return false;
    }

    bool SetPlacementPosition(std::string_view placementId, float x, float y, float z) {
        if (placementId.empty()) return false;
        Vector3 pos(x, y, z);

        auto it = g_spawned.find(std::string(placementId));
        if (it != g_spawned.end() && it->second) {
            auto* screen = it->second.ptr();
            screen->set_ScreenPosition(pos);
            return UpdatePlacementFromScreen(placementId, screen, true);
        }

        return SetPlacementPositionInConfig(placementId, pos, true);
    }

    bool SetPlacementRotation(std::string_view placementId, float x, float y, float z) {
        if (placementId.empty()) return false;
        Vector3 rotEuler(x, y, z);

        auto it = g_spawned.find(std::string(placementId));
        if (it != g_spawned.end() && it->second) {
            auto* screen = it->second.ptr();
            screen->set_ScreenRotation(Quaternion::Euler(rotEuler));
            return UpdatePlacementFromScreen(placementId, screen, true);
        }

        return SetPlacementRotationInConfig(placementId, rotEuler, true);
    }

    bool SetPlacementSize(std::string_view placementId, float width, float height) {
        if (placementId.empty()) return false;

        if (width < 10.0f) width = 10.0f;
        if (height < 10.0f) height = 10.0f;
        if (width > 400.0f) width = 400.0f;
        if (height > 400.0f) height = 400.0f;

        Vector2 size(width, height);

        auto it = g_spawned.find(std::string(placementId));
        if (it != g_spawned.end() && it->second) {
            auto* screen = it->second.ptr();
            screen->set_ScreenSize(size);
            ConfigureHandle(screen, size);
            return UpdatePlacementFromScreen(placementId, screen, true);
        }

        return SetPlacementSizeInConfig(placementId, size, true);
    }

    bool RecenterPlacement(std::string_view placementId) {
        if (placementId.empty()) return false;
        if (!GetPlacementCopy(placementId)) return false;

        RecenterPlacement(std::string(placementId));
        return true;
    }

    void EnsureRuntimeManager() {
        if (g_manager) return;

        auto go = GameObject::New_ctor(StringW("ImagerRuntimeManager"));
        if (!go) return;

        Object::DontDestroyOnLoad(go);
        auto* mgr = go->AddComponent<RuntimeManager*>();
        g_manager = mgr;
    }

    void SpawnSavedPlacements() {
        if (g_spawnedSaved) return;
        g_spawnedSaved = true;

        IO::EnsureImagesDirectory();

        auto placements = getModConfig().Placements.GetValue();
        std::vector<Placement> kept;
        kept.reserve(placements.size());

        bool didChange = false;

        for (auto p : placements) {
            auto fullPath = IO::GetImagesDirectory() + p.FileName;
            if (!std::filesystem::exists(fullPath)) {
                Logger.warn("Placement {} missing file '{}', removing from config", p.Id, p.FileName);
                didChange = true;
                continue;
            }

            if (IsZeroVec3(p.HomePosition) && !IsZeroVec3(p.Position)) {
                p.HomePosition = p.Position;
                didChange = true;
            }
            if (IsZeroVec3(p.HomeRotation) && !IsZeroVec3(p.Rotation)) {
                p.HomeRotation = p.Rotation;
                didChange = true;
            }

            kept.emplace_back(p);
            SpawnPlacementObject(p, false);
        }

        if (didChange) {
            getModConfig().Placements.SetValue(kept, true);
        }

        ApplySceneVisibility();
    }

    void LoadMapPlacementForSong(std::string_view songName) {
        EnsureRuntimeManager();
        RemovePlacement(kMapPlacementId);
        if (!getModConfig().Enabled.GetValue() || songName.empty()) {
            Logger.info("Map image disabled or no song selected");
            return;
        }

        std::error_code ec;
        auto root = std::filesystem::path(kCustomLevelsRoot);
        if (!std::filesystem::is_directory(root, ec)) {
            Logger.warn("Custom levels directory is unavailable: {}", root.string());
            return;
        }

        for (auto const& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec || !entry.is_directory()) continue;
            auto infoPath = entry.path() / "Info.dat";
            if (!std::filesystem::is_regular_file(infoPath, ec)) {
                infoPath = entry.path() / "info.dat";
            }
            if (!std::filesystem::is_regular_file(infoPath, ec)) continue;

            std::ifstream input(infoPath, std::ios::binary);
            std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            rapidjson::Document document;
            if (document.Parse(json.c_str(), json.size()).HasParseError() || !document.IsObject()) continue;
            if (!document.HasMember("_songName") || !document["_songName"].IsString()) continue;
            if (songName != document["_songName"].GetString()) continue;
            if (!document.HasMember("_customData") || !document["_customData"].IsObject()) break;
            auto const& custom = document["_customData"];
            if (!custom.HasMember("_mapImage") || !custom["_mapImage"].IsObject()) break;
            auto const& image = custom["_mapImage"];
            if (image.HasMember("_enabled") && image["_enabled"].IsBool() && !image["_enabled"].GetBool()) break;
            if (!image.HasMember("_file") || !image["_file"].IsString()) break;

            std::string fileName = image["_file"].GetString();
            auto relative = std::filesystem::path(fileName);
            if (relative.is_absolute() || relative.has_parent_path() || relative.filename().string() != fileName) {
                Logger.warn("Rejected unsafe map image file name: {}", fileName);
                break;
            }
            auto extension = relative.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            if (extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
                Logger.warn("Map image must be PNG or JPEG: {}", fileName);
                break;
            }
            auto imagePath = entry.path() / relative;
            if (!std::filesystem::is_regular_file(imagePath, ec)) {
                Logger.warn("Map image is missing: {}", imagePath.string());
                break;
            }

            auto vector = [&](char const* key, Vector3 fallback) {
                if (!image.HasMember(key) || !image[key].IsArray() || image[key].Size() < 2) return fallback;
                auto const& value = image[key];
                float z = value.Size() >= 3 && value[2].IsNumber() ? value[2].GetFloat() : fallback.z;
                if (!value[0].IsNumber() || !value[1].IsNumber()) return fallback;
                return Vector3(value[0].GetFloat(), value[1].GetFloat(), z);
            };

            Placement placement;
            placement.Id = std::string(kMapPlacementId);
            placement.FileName = imagePath.string();
            placement.Name = std::string(songName);
            placement.Position = vector("_position", Vector3(0.0f, 11.0f, 45.0f));
            placement.Rotation = vector("_rotation", Vector3(0.0f, 0.0f, 0.0f));
            placement.Scale = vector("_scale", Vector3(3200.0f, 2133.0f, 1.0f));
            placement.HomePosition = placement.Position;
            placement.HomeRotation = placement.Rotation;
            placement.ShowInMenu = false;
            placement.ShowInMap = true;
            SpawnPlacementObject(placement, false);
            ApplySceneVisibility();
            Logger.info("Loaded map-owned image for '{}': {}", songName, imagePath.string());
            return;
        }
        Logger.info("No enabled _mapImage found for '{}'", songName);
    }

    void SummonImage(std::string_view fileName) {
        EnsureRuntimeManager();
        IO::EnsureImagesDirectory();

        Placement p;
        p.Id = NewPlacementId();
        p.FileName = std::string(fileName);

        auto cam = Camera::get_main();
        if (cam) {
            auto ct = cam->get_transform();
            auto forward = ct->get_forward();
            p.Position = Add(ct->get_position(), Mul(forward, 2.0f));
            p.Rotation = Quaternion::LookRotation(forward).get_eulerAngles();
        } else {
            p.Position = Vector3(0.0f, 1.5f, 2.0f);
            p.Rotation = Vector3(0.0f, 180.0f, 0.0f);
        }

        p.HomePosition = p.Position;
        p.HomeRotation = p.Rotation;

        p.ShowInMenu = true;
        p.ShowInMap = true;

        auto placements = getModConfig().Placements.GetValue();
        placements.emplace_back(p);
        getModConfig().Placements.SetValue(placements, true);

        SpawnPlacementObject(p, true);
        ApplySceneVisibility();
    }

    void RuntimeManager::Awake() {
        g_manager = this;
        g_lastSceneName = GetActiveSceneName();
        g_lastMapContext = ComputeMapContext();
    }

    void RuntimeManager::Update() {
        auto sceneName = GetActiveSceneName();
        bool mapContext = ComputeMapContext();

        bool changed = false;
        if (!sceneName.empty() && sceneName != g_lastSceneName) {
            g_lastSceneName = sceneName;
            changed = true;
        }
        if (mapContext != g_lastMapContext) {
            g_lastMapContext = mapContext;
            changed = true;
        }

        if (changed) ApplySceneVisibility();
    }
}
