#include "UI/ImagerViewController.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <exception>
#include <vector>

#include "assets.hpp"
#include "logger.hpp"

#include "Imager/IO.hpp"
#include "Imager/RuntimeManager.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML/Components/Settings/DropdownListSetting.hpp"
#include "bsml/shared/BSML/SharedCoroutineStarter.hpp"
#include "bsml/shared/Helpers/creation.hpp"

#include "UI/AppEvents.hpp"
#include "UI/LibraryState.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Networking/DownloadHandler.hpp"
#include "UnityEngine/Networking/UnityWebRequest.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"

DEFINE_TYPE(Imager::UI, ImagerViewController);

namespace Imager::UI {
    static UnityEngine::Networking::UnityWebRequest* g_activeRequest = nullptr;
    static bool g_cancelRequested = false;
    static bool g_isDownloading = false;

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

    static std::optional<std::string> NormalizeToHttpsUrl(std::string_view url) {
        url = Trim(url);
        if (url.empty()) return std::nullopt;

        std::string s(url);

        if (s.starts_with("https://")) return s;
        if (s.starts_with("http://")) return "https://" + s.substr(7);
        if (s.starts_with("//")) return "https:" + s;

        if (s.find("://") != std::string::npos) return std::nullopt;

        return "https://" + s;
    }

    static std::string_view StripQueryAndFragment(std::string_view url) {
        auto q = url.find_first_of("?#");
        if (q == std::string_view::npos) return url;
        return url.substr(0, q);
    }

    static std::string ExtractUrlFileName(std::string_view url) {
        auto stripped = StripQueryAndFragment(url);
        auto slash = stripped.find_last_of('/');
        std::string_view name = (slash == std::string_view::npos) ? stripped : stripped.substr(slash + 1);
        if (name.empty()) return "image";
        return std::string(name);
    }

    static std::string ExtractUrlExtensionWithDotLower(std::string_view url) {
        auto stripped = StripQueryAndFragment(url);
        auto dot = stripped.find_last_of('.');
        if (dot == std::string_view::npos) return ".png";
        auto ext = ToLower(stripped.substr(dot));
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif") return ext;
        return ".png";
    }

    static void SetMainStatus(ImagerViewController* self, std::string_view msg) {
        if (!self) return;
        if (self->statusText) {
            self->statusText->set_text(StringW(msg));
            self->statusText->get_gameObject()->SetActive(!msg.empty());
        }
        if (!msg.empty()) Logger.info("UI: {}", msg);
    }

    static void EnsureDownloadModal(ImagerViewController* self) {
        if (!self) return;
        if (self->downloadModal) return;

        auto* modal = BSML::Lite::CreateModal(self->get_transform(), UnityEngine::Vector2(0.0f, 0.0f), UnityEngine::Vector2(80.0f, 36.0f), nullptr, false);
        if (!modal) return;

        modal->moveToCenter = true;
        modal->dismissOnBlockerClicked = false;

        self->downloadModal = modal;

        auto* contentGo = UnityEngine::GameObject::New_ctor(StringW("ImagerDownloadModalContent"));
        if (!contentGo) return;

        contentGo->get_transform()->SetParent(modal->get_transform(), false);
        if (auto* rt = contentGo->AddComponent<UnityEngine::RectTransform*>()) {
            rt->set_anchorMin(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_anchorMax(UnityEngine::Vector2(1.0f, 1.0f));
            rt->set_anchoredPosition(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_sizeDelta(UnityEngine::Vector2(0.0f, 0.0f));
        }

        BSML::parse_and_construct(IncludedAssets::download_modal_bsml, contentGo->get_transform(), self);

        modal->Hide();
    }

    static void SetDownloadStatus(ImagerViewController* self, std::string_view msg) {
        if (!self) return;
        EnsureDownloadModal(self);
        if (self->downloadStatusText) self->downloadStatusText->set_text(StringW(msg));
        if (!msg.empty()) Logger.info("UI: {}", msg);
    }

    static void ShowDownloadModal(ImagerViewController* self, bool show) {
        if (!self) return;
        EnsureDownloadModal(self);
        if (!self->downloadModal) return;
        if (show) self->downloadModal->Show();
        else self->downloadModal->Hide();
    }

    static void EnsureFileActionsModal(ImagerViewController* self) {
        if (!self) return;
        if (self->fileActionsModal) return;

        auto* modal = BSML::Lite::CreateModal(self->get_transform(), UnityEngine::Vector2(0.0f, 0.0f), UnityEngine::Vector2(72.0f, 44.0f), nullptr, true);
        if (!modal) return;

        modal->moveToCenter = true;
        modal->dismissOnBlockerClicked = true;
        self->fileActionsModal = modal;

        auto* contentGo = UnityEngine::GameObject::New_ctor(StringW("ImagerFileActionsModalContent"));
        if (!contentGo) return;

        contentGo->get_transform()->SetParent(modal->get_transform(), false);
        if (auto* rt = contentGo->AddComponent<UnityEngine::RectTransform*>()) {
            rt->set_anchorMin(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_anchorMax(UnityEngine::Vector2(1.0f, 1.0f));
            rt->set_anchoredPosition(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_sizeDelta(UnityEngine::Vector2(0.0f, 0.0f));
        }

        BSML::parse_and_construct(IncludedAssets::file_actions_modal_bsml, contentGo->get_transform(), self);
        modal->Hide();
    }

    static void ShowFileActionsModal(ImagerViewController* self, bool show) {
        if (!self) return;
        EnsureFileActionsModal(self);
        if (!self->fileActionsModal) return;
        if (show) self->fileActionsModal->Show();
        else self->fileActionsModal->Hide();
    }

    static bool HasLiteKeyboardPrefab() {
        static int state = -1;  // -1 unknown, 0 missing, 1 found
        if (state != -1) return state == 1;

        auto allFields = UnityEngine::Resources::FindObjectsOfTypeAll<HMUI::InputFieldView*>();
        if (!allFields) {
            state = 0;
            return false;
        }

        for (auto* field : allFields) {
            if (!field) continue;
            auto name = field->get_name();
            if (!name) continue;
            if ((std::string)name == "GuestNameInputField") {
                state = 1;
                return true;
            }
        }

        state = 0;
        return false;
    }

    static void EnsureDownloadUrlInput(ImagerViewController* self) {
        if (!self) return;
        if (self->downloadUrlInput) return;
        if (!self->downloadUrlKeyboardHost) return;
        if (self->downloadUrlKeyboardHost->get_childCount() > 0) return;

        if (!HasLiteKeyboardPrefab()) {
            try {
                BSML::parse_and_construct(
                    "<string-setting text='URL' value='downloadUrl' apply-on-change='true' pref-width='90'/>",
                    self->downloadUrlKeyboardHost,
                    self
                );
                Logger.warn("BSML-Lite keyboard prefab missing/using XML string setting");
            } catch (const std::exception& e) {
                Logger.error(";( URL input fallback failed: {}", e.what());
            } catch (...) {
                Logger.error(";( URL input fallback failed: unknown");
            }
            return;
        }

        auto safeSelf = SafePtrUnity<ImagerViewController>(self);
        HMUI::InputFieldView* input = nullptr;
        try {
            input = BSML::Lite::CreateStringSetting(
                self->downloadUrlKeyboardHost,
                StringW("URL"),
                self->get_downloadUrl() ? self->get_downloadUrl() : StringW(""),
                UnityEngine::Vector2(0.0f, 0.0f),
                UnityEngine::Vector3(0.0f, 0.0f, 0.0f),
                [safeSelf](StringW value) {
                    if (!safeSelf) return;
                    safeSelf.ptr()->set_downloadUrl(value);
                }
            );
        } catch (const std::exception& e) {
            Logger.error(";( Failed to create BSML-Lite URL input: {}", e.what());
        } catch (...) {
            Logger.error(";( Failed to create BSML-Lite URL input: unknown");
        }
        if (!input) return;

        self->downloadUrlInput = input;
    }

    static void RefreshLibrary(ImagerViewController* self) {
        LibraryState::Refresh();
        auto const& allImages = LibraryState::GetImages();

        if (self) {
            auto* selObj = self->get_selectedImage();
            std::string sel;
            if (selObj) sel = (std::string)StringW(reinterpret_cast<Il2CppString*>(selObj));

            bool selectionValid = !sel.empty() && std::find(allImages.begin(), allImages.end(), sel) != allImages.end();
            if (!selectionValid) {
                if (!allImages.empty()) {
                    self->set_selectedImage(reinterpret_cast<System::Object*>(StringW(allImages.front()).convert()));
                } else {
                    self->set_selectedImage(nullptr);
                }
            }
        }

        if (self && self->imageDropdown) {
            self->imageDropdown->values = self->get_imageChoices();
            self->imageDropdown->set_interactable(!allImages.empty());
            self->imageDropdown->UpdateChoices();
            self->imageDropdown->ReceiveValue();
            self->imageDropdown->UpdateState();
        }
    }

    static std::string DetectExtensionFromContentType(std::string_view contentTypeLower) {
        if (contentTypeLower.find("image/png") != std::string::npos) return ".png";
        if (contentTypeLower.find("image/jpeg") != std::string::npos) return ".jpg";
        if (contentTypeLower.find("image/jpg") != std::string::npos) return ".jpg";
        if (contentTypeLower.find("image/gif") != std::string::npos) return ".gif";
        return {};
    }

    static std::string DetectExtensionFromBytes(std::span<std::uint8_t const> bytes) {
        if (bytes.size() >= 8) {
            const std::uint8_t pngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
            bool isPng = true;
            for (int i = 0; i < 8; ++i) {
                if (bytes[i] != pngSig[i]) {
                    isPng = false;
                    break;
                }
            }
            if (isPng) return ".png";
        }
        if (bytes.size() >= 3) {
            if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return ".jpg";
        }
        if (bytes.size() >= 6) {
            bool gif87 = bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8' && bytes[4] == '7' && bytes[5] == 'a';
            bool gif89 = bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8' && bytes[4] == '9' && bytes[5] == 'a';
            if (gif87 || gif89) return ".gif";
        }
        return {};
    }

    static std::optional<std::string> ExtractMetaImageUrl(std::span<std::uint8_t const> bytes) {
        if (bytes.empty()) return std::nullopt;

        size_t len = bytes.size();
        if (len > 512 * 1024) len = 512 * 1024;

        std::string html(reinterpret_cast<char const*>(bytes.data()), len);
        auto lower = ToLower(html);

        auto extractContent = [&](std::string_view tagLower, std::string_view tagOrig) -> std::optional<std::string> {
            auto cpos = tagLower.find("content=");
            if (cpos == std::string::npos) return std::nullopt;
            cpos += 8;  // strlen("content=")
            if (cpos >= tagLower.size()) return std::nullopt;

            char quote = tagLower[cpos];
            size_t start = cpos;
            size_t end = std::string::npos;

            if (quote == '"' || quote == '\'') {
                start = cpos + 1;
                end = tagLower.find(quote, start);
            } else {
                end = tagLower.find_first_of(" \r\n\t>", start);
            }

            if (end == std::string::npos || end <= start) return std::nullopt;

            std::string url = std::string(tagOrig.substr(start, end - start));
            url = (std::string)Trim(url);

            for (size_t p = 0; (p = url.find("&amp;", p)) != std::string::npos;) {
                url.replace(p, 5, "&");
                p += 1;
            }

            if (url.starts_with("//")) url = "https:" + url;

            auto httpsUrl = NormalizeToHttpsUrl(url);
            if (!httpsUrl) return std::nullopt;
            return *httpsUrl;
        };

        static constexpr std::string_view markers[] = {
            "property=\"og:image\"",
            "property='og:image'",
            "name=\"twitter:image\"",
            "name='twitter:image'",
        };

        for (auto marker : markers) {
            auto pos = lower.find(marker);
            if (pos == std::string::npos) continue;

            size_t tagStart = lower.rfind("<meta", pos);
            if (tagStart == std::string::npos) tagStart = pos;
            size_t tagEnd = lower.find('>', pos);
            if (tagEnd == std::string::npos) {
                tagEnd = std::min(lower.size(), pos + 1024);
            } else {
                tagEnd += 1;
            }

            auto tagLower = std::string_view(lower).substr(tagStart, tagEnd - tagStart);
            auto tagOrig = std::string_view(html).substr(tagStart, tagEnd - tagStart);
            auto url = extractContent(tagLower, tagOrig);
            if (url) return url;
        }

        return std::nullopt;
    }

    static bool LooksLikeHtml(std::span<std::uint8_t const> bytes) {
        size_t i = 0;
        while (i < bytes.size() && std::isspace((unsigned char)bytes[i])) ++i;
        if (i >= bytes.size()) return false;
        return bytes[i] == '<';
    }

    custom_types::Helpers::Coroutine DownloadAndSaveCoroutine(SafePtrUnity<ImagerViewController> self, StringW url, std::string suggestedName) {
        using namespace UnityEngine::Networking;
        static constexpr size_t kMaxDownloadBytes = 16 * 1024 * 1024;
        static constexpr size_t kMaxGifBytes = 15 * 1024 * 1024;

        g_isDownloading = true;
        if (self) {
            ShowDownloadModal(self.ptr(), true);
            SetDownloadStatus(self.ptr(), "Downloading image...");
        }

        auto finish = [&](std::string_view msg) {
            g_isDownloading = false;
            if (self) {
                ShowDownloadModal(self.ptr(), false);
                SetMainStatus(self.ptr(), msg);
            }
        };

        std::vector<std::uint8_t> downloadedBytes;
        std::string finalUrl;
        std::string contentTypeLower;

        auto* req = UnityWebRequest::Get(url);
        if (!req) {
            finish("Download failed: request creation failed");
            co_return;
        }

        g_activeRequest = req;
        req->set_timeout(20);
        req->set_redirectLimit(5);
        req->SendWebRequest();

        while (!req->get_isDone()) {
            if (g_cancelRequested) req->Abort();
            co_yield nullptr;
        }
        g_activeRequest = nullptr;

        if (g_cancelRequested) {
            req->Dispose();
            finish("Download cancelled.");
            co_return;
        }

        if (req->get_result() != UnityWebRequest_Result::Success) {
            auto err = (std::string)req->get_error();
            req->Dispose();
            finish(std::string("Download failed: ") + err);
            co_return;
        }

        auto* dh = req->get_downloadHandler();
        auto data = dh ? dh->get_data() : ArrayW<std::uint8_t>();
        if (!data || data.size() == 0) {
            req->Dispose();
            finish("Download failed: empty response");
            co_return;
        }

        downloadedBytes.assign(data.begin(), data.end());
        if (downloadedBytes.size() > kMaxDownloadBytes) {
            req->Dispose();
            finish("Download failed: image is too large (max 16MB).");
            co_return;
        }
        if (auto ct = req->GetResponseHeader("Content-Type"); ct) contentTypeLower = ToLower((std::string)ct);
        if (auto fu = req->get_url(); fu) finalUrl = (std::string)fu;
        req->Dispose();

        std::string ext = DetectExtensionFromContentType(contentTypeLower);
        auto firstBytesSpan = std::span<std::uint8_t const>(downloadedBytes.data(), downloadedBytes.size());
        if (ext.empty()) ext = DetectExtensionFromBytes(firstBytesSpan);
        if (ext == ".gif" && downloadedBytes.size() > kMaxGifBytes) {
            finish("Download failed: GIF is too large (max 15MB).");
            co_return;
        }

        if (ext.empty()) {
            bool maybeHtml = contentTypeLower.find("text/html") != std::string::npos || LooksLikeHtml(firstBytesSpan);
            auto metaUrl = maybeHtml ? ExtractMetaImageUrl(firstBytesSpan) : std::nullopt;
            if (!metaUrl) {
                finish("Download failed: URL did not give a PNG/JPG/GIF image.");
                co_return;
            }

            if (self) SetDownloadStatus(self.ptr(), "Following page to image...");

            auto metaUrlHttps = NormalizeToHttpsUrl(*metaUrl);
            if (!metaUrlHttps) {
                finish("Download failed: invalid image URL.");
                co_return;
            }

            auto* req2 = UnityWebRequest::Get(StringW(*metaUrlHttps));
            if (!req2) {
                finish("Download failed: request creation failed");
                co_return;
            }

            g_activeRequest = req2;
            req2->set_timeout(20);
            req2->set_redirectLimit(5);
            req2->SendWebRequest();

            while (!req2->get_isDone()) {
                if (g_cancelRequested) req2->Abort();
                co_yield nullptr;
            }
            g_activeRequest = nullptr;

            if (g_cancelRequested) {
                req2->Dispose();
                finish("Download cancelled.");
                co_return;
            }

            if (req2->get_result() != UnityWebRequest_Result::Success) {
                auto err = (std::string)req2->get_error();
                req2->Dispose();
                finish(std::string("Download failed: ") + err);
                co_return;
            }

            auto* dh2 = req2->get_downloadHandler();
            auto data2 = dh2 ? dh2->get_data() : ArrayW<std::uint8_t>();
            if (!data2 || data2.size() == 0) {
                req2->Dispose();
                finish("Download failed: empty response");
                co_return;
            }

            downloadedBytes.assign(data2.begin(), data2.end());
            if (downloadedBytes.size() > kMaxDownloadBytes) {
                req2->Dispose();
                finish("Download failed: image is too large (max 16MB).");
                co_return;
            }
            contentTypeLower.clear();
            if (auto ct2 = req2->GetResponseHeader("Content-Type"); ct2) contentTypeLower = ToLower((std::string)ct2);
            finalUrl.clear();
            if (auto fu2 = req2->get_url(); fu2) finalUrl = (std::string)fu2;
            req2->Dispose();

            ext = DetectExtensionFromContentType(contentTypeLower);
            if (ext.empty()) ext = DetectExtensionFromBytes(std::span<std::uint8_t const>(downloadedBytes.data(), downloadedBytes.size()));
            if (ext == ".gif" && downloadedBytes.size() > kMaxGifBytes) {
                finish("Download failed: GIF is too large (max 15MB).");
                co_return;
            }

            if (ext.empty()) {
                finish("Download failed: URL did not give a PNG/JPG/GIF image.");
                co_return;
            }

            suggestedName = ExtractUrlFileName(*metaUrlHttps);
        }

        if (!Imager::IO::EnsureImagesDirectory()) {
            finish("Save failed: could not create images directory.");
            co_return;
        }

        std::string nameFromUrl = finalUrl.empty() ? std::string{} : ExtractUrlFileName(finalUrl);
        std::string baseName = !nameFromUrl.empty() ? nameFromUrl : suggestedName;
        if (baseName.empty()) baseName = "image";

        auto fileName = Imager::IO::MakeUniqueImageFileName(baseName, ext);
        auto fullPath = Imager::IO::GetImagesDirectory() + fileName;

        if (self) SetDownloadStatus(self.ptr(), "Saving...");

        std::string ioErr;
        if (!Imager::IO::WriteAllBytes(fullPath, std::span<std::uint8_t const>(downloadedBytes.data(), downloadedBytes.size()), ioErr)) {
            finish(std::string("Save failed: ") + ioErr);
            co_return;
        }

        if (self) {
            ShowDownloadModal(self.ptr(), false);
            SetMainStatus(self.ptr(), std::string("Saved: ") + fileName);
            RefreshLibrary(self.ptr());
            self->set_selectedImage(reinterpret_cast<System::Object*>(StringW(fileName).convert()));
            if (self->imageDropdown) {
                self->imageDropdown->values = self->get_imageChoices();
                self->imageDropdown->ReceiveValue();
                self->imageDropdown->UpdateState();
            }
            NotifyLibraryChanged();
        }

        g_isDownloading = false;
        co_return;
    }

    void ImagerViewController::DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
        (void)addedToHierarchy;
        (void)screenSystemEnabling;

        try {
            Imager::EnsureRuntimeManager();

            if (firstActivation) {
                set_editMode(Imager::GetEditMode());
                BSML::parse_and_construct(IncludedAssets::settings_bsml, this->get_transform(), this);
            }
            EnsureDownloadUrlInput(this);

            EnsureDownloadModal(this);
            ShowDownloadModal(this, false);
            EnsureFileActionsModal(this);
            ShowFileActionsModal(this, false);

            SetMainStatus(this, "Copy a URL containing a direct .jpg/.png/.gif image");
            RefreshLibrary(this);
        } catch (const std::exception& e) {
            Logger.error("ImagerViewController::DidActivate failed: {}", e.what());
        } catch (...) {
            Logger.error("ImagerViewController::DidActivate failed with unknown exception");
        }
    }

    void ImagerViewController::DidDeactivate(bool removedFromHierarchy, bool screenSystemDisabling) {
        (void)removedFromHierarchy;
        (void)screenSystemDisabling;
        ShowDownloadModal(this, false);
        ShowFileActionsModal(this, false);
    }

    StringW ImagerViewController::get_downloadUrl() {
        return _downloadUrl;
    }
    void ImagerViewController::set_downloadUrl(StringW value) {
        _downloadUrl = value;
        if (downloadUrlInput && downloadUrlInput->get_text() != value) {
            downloadUrlInput->SetText(value);
        }
    }

    System::Object* ImagerViewController::get_selectedImage() {
        return _selectedImage;
    }
    void ImagerViewController::set_selectedImage(System::Object* value) {
        _selectedImage = value;
    }

    bool ImagerViewController::get_editMode() {
        return _editMode;
    }
    void ImagerViewController::set_editMode(bool value) {
        _editMode = value;
    }

    ListW<System::Object*> ImagerViewController::get_imageChoices() {
        LibraryState::Refresh();
        auto const& allImages = LibraryState::GetImages();

        auto list = ListW<System::Object*>::New();
        if (allImages.empty()) {
            list->EnsureCapacity(1);
            list->Add(reinterpret_cast<System::Object*>(StringW("No images").convert()));
        } else {
            list->EnsureCapacity(allImages.size());
            for (auto const& img : allImages) {
                list->Add(reinterpret_cast<System::Object*>(StringW(img).convert()));
            }
        }
        return list;
    }

    void ImagerViewController::OnDownloadClicked() {
        if (g_isDownloading) {
            SetMainStatus(this, "Already downloading.");
            return;
        }

        if (downloadUrlInput) set_downloadUrl(downloadUrlInput->get_text());

        std::string urlStr;
        auto dl = get_downloadUrl();
        if (dl) urlStr = (std::string)dl;
        urlStr = (std::string)Trim(urlStr);

        auto httpsUrlOpt = NormalizeToHttpsUrl(urlStr);
        if (!httpsUrlOpt) {
            SetMainStatus(this, "https:// is not required! Just paste the URL as it appears in your browser");
            return;
        }

        auto httpsUrl = *httpsUrlOpt;
        auto suggested = ExtractUrlFileName(httpsUrl);

        g_isDownloading = true;
        g_cancelRequested = false;
        g_activeRequest = nullptr;
        EnsureDownloadModal(this);
        ShowDownloadModal(this, true);
        SetDownloadStatus(this, "Starting download...");

        BSML::SharedCoroutineStarter::StartCoroutine(DownloadAndSaveCoroutine(SafePtrUnity<ImagerViewController>(this), StringW(httpsUrl), suggested));
    }

    void ImagerViewController::OnCancelDownloadClicked() {
        if (!g_isDownloading) return;
        g_cancelRequested = true;
        if (g_activeRequest) g_activeRequest->Abort();
        SetDownloadStatus(this, "Cancelling...");
    }

    void ImagerViewController::OnOpenFileActionsClicked() {
        ShowFileActionsModal(this, true);
    }

    void ImagerViewController::OnCloseFileActionsClicked() {
        ShowFileActionsModal(this, false);
    }

    void ImagerViewController::OnEditModeChanged(bool value) {
        Imager::SetEditMode(value);
    }

    void ImagerViewController::OnRemovePlacedForSelectedClicked() {
        auto* selObj = get_selectedImage();
        if (!selObj) {
            SetMainStatus(this, "No image selected.");
            return;
        }

        auto fileName = (std::string)StringW(reinterpret_cast<Il2CppString*>(selObj));
        if (!Imager::IO::IsSupportedImageFile(fileName)) {
            SetMainStatus(this, "No image selected.");
            return;
        }

        Imager::RemovePlacementsForFile(fileName);
        SetMainStatus(this, std::string("Removed placed: ") + fileName);
    }

    void ImagerViewController::OnDeleteFileClicked() {
        auto* selObj = get_selectedImage();
        if (!selObj) {
            SetMainStatus(this, "No image selected.");
            return;
        }

        auto fileName = (std::string)StringW(reinterpret_cast<Il2CppString*>(selObj));
        if (!Imager::IO::IsSupportedImageFile(fileName)) {
            SetMainStatus(this, "No image selected.");
            return;
        }

        Imager::RemovePlacementsForFile(fileName);

        std::string err;
        if (!Imager::IO::DeleteImageFile(fileName, err)) {
            SetMainStatus(this, std::string("Delete failed: ") + err);
            return;
        }

        SetMainStatus(this, std::string("Deleted: ") + fileName);
        RefreshLibrary(this);
        NotifyLibraryChanged();
    }

    void ImagerViewController::OnClearPlacedClicked() {
        Imager::ClearAllPlacements();
        SetMainStatus(this, "Cleared all placed images.");
    }

    void ImagerViewController::OnBackClicked() {
        if (flowCoordinator) flowCoordinator->BackButtonWasPressed(this);
    }
}
