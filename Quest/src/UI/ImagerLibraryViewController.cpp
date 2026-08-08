#include "UI/ImagerLibraryViewController.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <exception>

#include "assets.hpp"
#include "logger.hpp"

#include "Imager/IO.hpp"
#include "Imager/RuntimeManager.hpp"
#include "UI/AppEvents.hpp"
#include "UI/LibraryState.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/creation.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "bsml/shared/BSML/Animations/AnimationLoader.hpp"
#include "bsml/shared/BSML/Animations/AnimationController.hpp"
#include "bsml/shared/BSML/Animations/AnimationControllerData.hpp"
#include "bsml/shared/BSML/Animations/AnimationStateUpdater.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "bsml/shared/BSML/Components/ClickableImage.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/GridLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"

DEFINE_TYPE(Imager::UI, ImagerLibraryViewController);

namespace Imager::UI {
    using namespace UnityEngine;

    struct GifPreviewTarget {
        SafePtrUnity<BSML::ClickableImage> image;
        bool startPlaying = false;
    };
    static std::unordered_map<std::string, std::vector<GifPreviewTarget>> g_pendingGifPreviewAttaches;
    static std::unordered_set<std::string> g_gifPreviewDecodeInProgress;

    static void SetGifPreviewPlaying(BSML::ClickableImage* img, bool playing) {
        if (!img) return;
        auto* updater = img->get_gameObject()->GetComponent<BSML::AnimationStateUpdater*>();
        if (!updater) return;

        auto* data = updater->get_controllerData();
        if (!data) return;

        data->set_isPlaying(playing);
    }

    static void ApplyGifDataToPreview(BSML::ClickableImage* img, BSML::AnimationControllerData* data, bool startPlaying) {
        if (!img || !data) return;
        auto* updater = img->get_gameObject()->GetComponent<BSML::AnimationStateUpdater*>();
        if (!updater) updater = img->get_gameObject()->AddComponent<BSML::AnimationStateUpdater*>();
        if (!updater) return;

        auto* image = reinterpret_cast<UnityEngine::UI::Image*>(img);
        updater->image = image;
        updater->set_controllerData(data);
        updater->set_enabled(true);
        data->set_isPlaying(startPlaying);
    }

    static void FinalizeGifPreviewDecode(std::string const& key, BSML::AnimationControllerData* dataOrNull) {
        auto it = g_pendingGifPreviewAttaches.find(key);
        if (it != g_pendingGifPreviewAttaches.end()) {
            for (auto const& target : it->second) {
                if (!target.image) continue;
                auto* img = target.image.ptr();
                if (!img || !img->m_CachedPtr.m_value) continue;
                if (dataOrNull) ApplyGifDataToPreview(img, dataOrNull, target.startPlaying);
            }
            g_pendingGifPreviewAttaches.erase(it);
        }
        g_gifPreviewDecodeInProgress.erase(key);
    }

    static void AttachGifPreviewFromFile(BSML::ClickableImage* img, std::string const& fileName, bool startPlaying) {
        if (!img) return;

        auto* image = reinterpret_cast<UnityEngine::UI::Image*>(img);

        auto* updater = img->get_gameObject()->GetComponent<BSML::AnimationStateUpdater*>();
        if (!updater) updater = img->get_gameObject()->AddComponent<BSML::AnimationStateUpdater*>();
        if (!updater) return;

        updater->image = image;
        updater->set_controllerData(nullptr);
        updater->set_enabled(false);

        std::string key = "imager_library_" + fileName;
        auto* controller = BSML::AnimationController::get_instance();
        if (controller) {
            BSML::AnimationControllerData* existing = nullptr;
            if (controller->TryGetAnimationControllerData(StringW(key), existing) && existing) {
                ApplyGifDataToPreview(img, existing, startPlaying);
                return;
            }
        }

        g_pendingGifPreviewAttaches[key].push_back(GifPreviewTarget{SafePtrUnity<BSML::ClickableImage>(img), startPlaying});
        if (!g_gifPreviewDecodeInProgress.insert(key).second) return;

        std::string ioErr;
        auto bytesOpt = Imager::IO::ReadAllBytes(Imager::IO::GetImagesDirectory() + fileName, ioErr);
        if (!bytesOpt || bytesOpt->empty()) {
            Logger.warn(";( Failed to read GIF preview '{}': {}", fileName, ioErr.empty() ? "empty file" : ioErr);
            FinalizeGifPreviewDecode(key, nullptr);
            return;
        }

        static constexpr size_t kMaxGifBytes = 15 * 1024 * 1024;
        if (bytesOpt->size() > kMaxGifBytes) {
            Logger.warn("GIF preview '{}' too large ({} bytes, max 15MB), skipping", fileName, bytesOpt->size());
            FinalizeGifPreviewDecode(key, nullptr);
            return;
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
                        Logger.warn(";( Failed to register GIF preview '{}'", fileName);
                    }
                    FinalizeGifPreviewDecode(key, dataObj);
                });
            },
            [key, fileName]() {
                BSML::MainThreadScheduler::Schedule([key, fileName]() {
                    Logger.warn(";( Failed to decode GIF preview '{}'", fileName);
                    FinalizeGifPreviewDecode(key, nullptr);
                });
            }
        );
    }

    static void ClearChildren(Transform* parent) {
        if (!parent) return;
        int count = parent->get_childCount();
        for (int i = count - 1; i >= 0; --i) {
            auto child = parent->GetChild(i);
            if (!child) continue;
            Object::Destroy(child->get_gameObject());
        }
    }

    void ImagerLibraryViewController::DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
        (void)addedToHierarchy;
        (void)screenSystemEnabling;

        try {
            if (firstActivation) {
                BSML::parse_and_construct(IncludedAssets::imager_library_bsml, this->get_transform(), this);
            }

            RegisterLibraryChangedCallback(this, [safe = SafePtrUnity<ImagerLibraryViewController>(this)]() {
                if (!safe) return;
                safe.ptr()->RefreshGrid();
            });

            LibraryState::Refresh();
            RefreshGrid();
        } catch (const std::exception& e) {
            Logger.error(";( ImagerLibraryViewController::DidActivate failed: {}", e.what());
        } catch (...) {
            Logger.error(";( ImagerLibraryViewController::DidActivate failed: unknown");
        }
    }

    void ImagerLibraryViewController::DidDeactivate(bool removedFromHierarchy, bool screenSystemDisabling) {
        (void)removedFromHierarchy;
        (void)screenSystemDisabling;
        UnregisterLibraryChangedCallback(this);
        LibraryState::ClearThumbnailCache();
    }

    void ImagerLibraryViewController::RefreshGrid() {
        if (!gridHost) return;

        LibraryState::Refresh();
        auto const& images = LibraryState::GetImages();

        ClearChildren(gridHost);

        if (emptyText) emptyText->get_gameObject()->SetActive(images.empty());
        if (images.empty()) return;

        auto* grid = BSML::Lite::CreateGridLayoutGroup(gridHost);
        if (!grid) return;

        grid->set_cellSize(Vector2(20.0f, 20.0f));
        grid->set_spacing(Vector2(1.5f, 1.5f));
        grid->set_constraint(UnityEngine::UI::GridLayoutGroup_Constraint::FixedColumnCount);
        grid->set_constraintCount(4);

        if (auto* fitter = grid->get_gameObject()->AddComponent<UnityEngine::UI::ContentSizeFitter*>()) {
            fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter_FitMode::Unconstrained);
            fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter_FitMode::PreferredSize);
        }

        for (auto const& fileName : images) {
            auto fn = fileName;
            bool isGif = Imager::IO::IsAnimatedImageFile(fileName);

            UnityEngine::Sprite* sprite = nullptr;
            if (isGif) {
                sprite = BSML::Utilities::ImageResources::GetWhitePixel();
            } else {
                sprite = LibraryState::GetOrCreateThumbnail(fileName);
            }
            if (!sprite) continue;

            auto* img = BSML::Lite::CreateClickableImage(grid->get_transform(), sprite, [fn]() {
                Imager::SummonImage(fn);
            });
            if (!img) continue;

            img->set_preserveAspect(true);

            if (isGif) {
                auto safeImg = SafePtrUnity<BSML::ClickableImage>(img);
                auto gifFile = fileName;
                img->onEnter = [safeImg, gifFile]() {
                    if (!safeImg) return;
                    AttachGifPreviewFromFile(safeImg.ptr(), gifFile, true);
                    SetGifPreviewPlaying(safeImg.ptr(), true);
                };
                img->onExit = [safeImg]() {
                    if (!safeImg) return;
                    SetGifPreviewPlaying(safeImg.ptr(), false);
                };
            }

            BSML::Lite::AddHoverHint(img->get_gameObject(), StringW(fn));
        }
    }
}
