#include "UI/ImagerPlacementSettingsViewController.hpp"

#include <string>
#include <exception>

#include "assets.hpp"
#include "logger.hpp"

#include "Imager/RuntimeManager.hpp"
#include "UI/AppEvents.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"

DEFINE_TYPE(Imager::UI, ImagerPlacementSettingsViewController);

namespace Imager::UI {
    static bool g_updatingPlacementUi = false;

    static std::string GetPlacementId(ImagerPlacementSettingsViewController* self) {
        if (!self) return {};
        if (!self->editingPlacementId) return {};
        return (std::string)self->editingPlacementId;
    }

    static void SetSettingsActive(ImagerPlacementSettingsViewController* self, bool active) {
        if (!self || !self->settingsRoot) return;
        self->settingsRoot->get_gameObject()->SetActive(active);
    }

    static void SetInfo(ImagerPlacementSettingsViewController* self, std::string_view msg) {
        if (!self || !self->placementInfoText) return;
        self->placementInfoText->set_text(StringW(msg));
        self->placementInfoText->get_gameObject()->SetActive(true);
    }

    static void SetInc(BSML::IncrementSetting* s, float v) {
        if (!s) return;
        s->set_Value(v);
        s->UpdateState();
    }

    static void HidePlacementModals(ImagerPlacementSettingsViewController* self) {
        if (!self) return;
        if (self->visibilityModal) self->visibilityModal->Hide();
        if (self->sizeModal) self->sizeModal->Hide();
        if (self->positionModal) self->positionModal->Hide();
        if (self->rotationModal) self->rotationModal->Hide();
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

    static void EnsureNameInput(ImagerPlacementSettingsViewController* self) {
        if (!self) return;
        if (self->nameInput) return;
        if (!self->nameKeyboardHost) return;

        if (!HasLiteKeyboardPrefab()) {
            Logger.warn("BSML(lite) keyboard prefab missing");
            return;
        }

        auto safeSelf = SafePtrUnity<ImagerPlacementSettingsViewController>(self);
        HMUI::InputFieldView* input = nullptr;
        try {
            input = BSML::Lite::CreateStringSetting(
                self->nameKeyboardHost,
                StringW("Name"),
                StringW(""),
                UnityEngine::Vector2(0.0f, 0.0f),
                UnityEngine::Vector3(0.0f, 0.0f, 0.0f),
                [safeSelf](StringW value) {
                    if (!safeSelf) return;
                    safeSelf.ptr()->OnNameChanged(value);
                }
            );
        } catch (const std::exception& e) {
            Logger.error(";( Failed to create BSML-Lite placement name input: {}", e.what());
        } catch (...) {
            Logger.error(";( Failed to create BSML-Lite placement name input: unknown");
        }
        if (!input) return;

        self->nameInput = input;
    }

    static void EnsureRotationModal(ImagerPlacementSettingsViewController* self) {
        if (!self) return;
        if (self->rotationModal) return;

        auto* modal = BSML::Lite::CreateModal(self->get_transform(), UnityEngine::Vector2(0.0f, 0.0f), UnityEngine::Vector2(86.0f, 54.0f), nullptr, true);
        if (!modal) return;
        modal->moveToCenter = true;
        modal->dismissOnBlockerClicked = true;
        self->rotationModal = modal;

        auto* contentGo = UnityEngine::GameObject::New_ctor(StringW("ImagerRotationModalContent"));
        if (!contentGo) return;

        contentGo->get_transform()->SetParent(modal->get_transform(), false);
        if (auto* rt = contentGo->AddComponent<UnityEngine::RectTransform*>()) {
            rt->set_anchorMin(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_anchorMax(UnityEngine::Vector2(1.0f, 1.0f));
            rt->set_anchoredPosition(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_sizeDelta(UnityEngine::Vector2(0.0f, 0.0f));
        }

        BSML::parse_and_construct(IncludedAssets::rotation_modal_bsml, contentGo->get_transform(), self);
        modal->Hide();
    }

    static void EnsureVisibilityModal(ImagerPlacementSettingsViewController* self) {
        if (!self) return;
        if (self->visibilityModal) return;

        auto* modal = BSML::Lite::CreateModal(self->get_transform(), UnityEngine::Vector2(0.0f, 0.0f), UnityEngine::Vector2(86.0f, 42.0f), nullptr, true);
        if (!modal) return;
        modal->moveToCenter = true;
        modal->dismissOnBlockerClicked = true;
        self->visibilityModal = modal;

        auto* contentGo = UnityEngine::GameObject::New_ctor(StringW("ImagerVisibilityModalContent"));
        if (!contentGo) return;

        contentGo->get_transform()->SetParent(modal->get_transform(), false);
        if (auto* rt = contentGo->AddComponent<UnityEngine::RectTransform*>()) {
            rt->set_anchorMin(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_anchorMax(UnityEngine::Vector2(1.0f, 1.0f));
            rt->set_anchoredPosition(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_sizeDelta(UnityEngine::Vector2(0.0f, 0.0f));
        }

        BSML::parse_and_construct(IncludedAssets::placement_visibility_modal_bsml, contentGo->get_transform(), self);
        modal->Hide();
    }

    static void EnsureSizeModal(ImagerPlacementSettingsViewController* self) {
        if (!self) return;
        if (self->sizeModal) return;

        auto* modal = BSML::Lite::CreateModal(self->get_transform(), UnityEngine::Vector2(0.0f, 0.0f), UnityEngine::Vector2(86.0f, 44.0f), nullptr, true);
        if (!modal) return;
        modal->moveToCenter = true;
        modal->dismissOnBlockerClicked = true;
        self->sizeModal = modal;

        auto* contentGo = UnityEngine::GameObject::New_ctor(StringW("ImagerSizeModalContent"));
        if (!contentGo) return;

        contentGo->get_transform()->SetParent(modal->get_transform(), false);
        if (auto* rt = contentGo->AddComponent<UnityEngine::RectTransform*>()) {
            rt->set_anchorMin(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_anchorMax(UnityEngine::Vector2(1.0f, 1.0f));
            rt->set_anchoredPosition(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_sizeDelta(UnityEngine::Vector2(0.0f, 0.0f));
        }

        BSML::parse_and_construct(IncludedAssets::placement_size_modal_bsml, contentGo->get_transform(), self);
        modal->Hide();
    }

    static void EnsurePositionModal(ImagerPlacementSettingsViewController* self) {
        if (!self) return;
        if (self->positionModal) return;

        auto* modal = BSML::Lite::CreateModal(self->get_transform(), UnityEngine::Vector2(0.0f, 0.0f), UnityEngine::Vector2(86.0f, 52.0f), nullptr, true);
        if (!modal) return;
        modal->moveToCenter = true;
        modal->dismissOnBlockerClicked = true;
        self->positionModal = modal;

        auto* contentGo = UnityEngine::GameObject::New_ctor(StringW("ImagerPositionModalContent"));
        if (!contentGo) return;

        contentGo->get_transform()->SetParent(modal->get_transform(), false);
        if (auto* rt = contentGo->AddComponent<UnityEngine::RectTransform*>()) {
            rt->set_anchorMin(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_anchorMax(UnityEngine::Vector2(1.0f, 1.0f));
            rt->set_anchoredPosition(UnityEngine::Vector2(0.0f, 0.0f));
            rt->set_sizeDelta(UnityEngine::Vector2(0.0f, 0.0f));
        }

        BSML::parse_and_construct(IncludedAssets::placement_position_modal_bsml, contentGo->get_transform(), self);
        modal->Hide();
    }

    static void ClearSelection(ImagerPlacementSettingsViewController* self) {
        if (!self) return;
        g_updatingPlacementUi = true;
        self->editingPlacementId = StringW("");
        if (self->nameInput) self->nameInput->SetText(StringW(""));
        SetSettingsActive(self, false);
        HidePlacementModals(self);
        SetInfo(self, "Tip: Enable Edit Mode and tap a placed image to edit it.");
        g_updatingPlacementUi = false;
    }

    static void Populate(ImagerPlacementSettingsViewController* self, Imager::PlacementInfo const& info) {
        if (!self) return;

        g_updatingPlacementUi = true;

        self->editingPlacementId = StringW(info.id);

        std::string header = "File: " + info.fileName;
        if (!info.name.empty()) header += "  (\"" + info.name + "\")";
        SetInfo(self, header);

        SetSettingsActive(self, true);

        if (self->nameInput) self->nameInput->SetText(StringW(info.name));
        if (self->showInMenuToggle) self->showInMenuToggle->set_Value(info.showInMenu);
        if (self->showInGameToggle) self->showInGameToggle->set_Value(info.showInGame);

        SetInc(self->widthSetting, info.width);
        SetInc(self->heightSetting, info.height);

        SetInc(self->posXSetting, info.posX);
        SetInc(self->posYSetting, info.posY);
        SetInc(self->posZSetting, info.posZ);

        SetInc(self->rotXSetting, info.rotX);
        SetInc(self->rotYSetting, info.rotY);
        SetInc(self->rotZSetting, info.rotZ);

        g_updatingPlacementUi = false;
    }

    static void OpenPlacement(ImagerPlacementSettingsViewController* self, std::string const& placementId) {
        if (!self) return;
        Imager::PlacementInfo info;
        if (!Imager::TryGetPlacementInfo(placementId, info)) {
            Logger.warn("Placement settings try for missing placement '{}'", placementId);
            ClearSelection(self);
            return;
        }
        Populate(self, info);
    }

    void ImagerPlacementSettingsViewController::DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
        (void)addedToHierarchy;
        (void)screenSystemEnabling;

        try {
            if (firstActivation) {
                BSML::parse_and_construct(IncludedAssets::placement_settings_bsml, this->get_transform(), this);
            }
            EnsureNameInput(this);

            RegisterEditPlacementRequestedCallback(this, [safe = SafePtrUnity<ImagerPlacementSettingsViewController>(this)](std::string const& placementId) {
                if (!safe) return;
                OpenPlacement(safe.ptr(), placementId);
            });

            ClearSelection(this);
        } catch (const std::exception& e) {
            Logger.error(";( ImagerPlacementSettingsViewController::DidActivate failed: {}", e.what());
        } catch (...) {
            Logger.error(";( ImagerPlacementSettingsViewController::DidActivate failed: unknown");
        }
    }

    void ImagerPlacementSettingsViewController::DidDeactivate(bool removedFromHierarchy, bool screenSystemDisabling) {
        (void)removedFromHierarchy;
        (void)screenSystemDisabling;
        UnregisterEditPlacementRequestedCallback(this);
        HidePlacementModals(this);
    }

    void ImagerPlacementSettingsViewController::OnNameChanged(StringW value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        Imager::SetPlacementName(pid, (std::string)value);
    }

    void ImagerPlacementSettingsViewController::OnShowInMenuChanged(bool value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        if (!Imager::SetPlacementShowInMenu(pid, value)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnShowInGameChanged(bool value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        if (!Imager::SetPlacementShowInGame(pid, value)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnOpenVisibilityClicked() {
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;

        EnsureVisibilityModal(this);
        if (!visibilityModal) return;

        Imager::PlacementInfo info;
        if (Imager::TryGetPlacementInfo(pid, info)) {
            g_updatingPlacementUi = true;
            if (showInMenuToggle) showInMenuToggle->set_Value(info.showInMenu);
            if (showInGameToggle) showInGameToggle->set_Value(info.showInGame);
            g_updatingPlacementUi = false;
        }

        HidePlacementModals(this);
        visibilityModal->Show();
    }

    void ImagerPlacementSettingsViewController::OnCloseVisibilityClicked() {
        if (visibilityModal) visibilityModal->Hide();
    }

    void ImagerPlacementSettingsViewController::OnWidthChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float h = heightSetting ? heightSetting->get_Value() : 50.0f;
        if (!Imager::SetPlacementSize(pid, value, h)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnHeightChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float w = widthSetting ? widthSetting->get_Value() : 50.0f;
        if (!Imager::SetPlacementSize(pid, w, value)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnOpenSizeClicked() {
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;

        EnsureSizeModal(this);
        if (!sizeModal) return;

        Imager::PlacementInfo info;
        if (Imager::TryGetPlacementInfo(pid, info)) {
            g_updatingPlacementUi = true;
            SetInc(widthSetting, info.width);
            SetInc(heightSetting, info.height);
            g_updatingPlacementUi = false;
        }

        HidePlacementModals(this);
        sizeModal->Show();
    }

    void ImagerPlacementSettingsViewController::OnCloseSizeClicked() {
        if (sizeModal) sizeModal->Hide();
    }

    void ImagerPlacementSettingsViewController::OnPosXChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float y = posYSetting ? posYSetting->get_Value() : 0.0f;
        float z = posZSetting ? posZSetting->get_Value() : 0.0f;
        if (!Imager::SetPlacementPosition(pid, value, y, z)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnPosYChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float x = posXSetting ? posXSetting->get_Value() : 0.0f;
        float z = posZSetting ? posZSetting->get_Value() : 0.0f;
        if (!Imager::SetPlacementPosition(pid, x, value, z)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnPosZChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float x = posXSetting ? posXSetting->get_Value() : 0.0f;
        float y = posYSetting ? posYSetting->get_Value() : 0.0f;
        if (!Imager::SetPlacementPosition(pid, x, y, value)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnOpenPositionClicked() {
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;

        EnsurePositionModal(this);
        if (!positionModal) return;

        Imager::PlacementInfo info;
        if (Imager::TryGetPlacementInfo(pid, info)) {
            g_updatingPlacementUi = true;
            SetInc(posXSetting, info.posX);
            SetInc(posYSetting, info.posY);
            SetInc(posZSetting, info.posZ);
            g_updatingPlacementUi = false;
        }

        HidePlacementModals(this);
        positionModal->Show();
    }

    void ImagerPlacementSettingsViewController::OnClosePositionClicked() {
        if (positionModal) positionModal->Hide();
    }

    void ImagerPlacementSettingsViewController::OnRotXChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float y = rotYSetting ? rotYSetting->get_Value() : 0.0f;
        float z = rotZSetting ? rotZSetting->get_Value() : 0.0f;
        if (!Imager::SetPlacementRotation(pid, value, y, z)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnRotYChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float x = rotXSetting ? rotXSetting->get_Value() : 0.0f;
        float z = rotZSetting ? rotZSetting->get_Value() : 0.0f;
        if (!Imager::SetPlacementRotation(pid, x, value, z)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnRotZChanged(float value) {
        if (g_updatingPlacementUi) return;
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        float x = rotXSetting ? rotXSetting->get_Value() : 0.0f;
        float y = rotYSetting ? rotYSetting->get_Value() : 0.0f;
        if (!Imager::SetPlacementRotation(pid, x, y, value)) ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnOpenRotationClicked() {
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;

        EnsureRotationModal(this);
        if (!rotationModal) return;

        Imager::PlacementInfo info;
        if (Imager::TryGetPlacementInfo(pid, info)) {
            g_updatingPlacementUi = true;
            SetInc(rotXSetting, info.rotX);
            SetInc(rotYSetting, info.rotY);
            SetInc(rotZSetting, info.rotZ);
            g_updatingPlacementUi = false;
        }

        HidePlacementModals(this);
        rotationModal->Show();
    }

    void ImagerPlacementSettingsViewController::OnCloseRotationClicked() {
        if (rotationModal) rotationModal->Hide();
    }

    void ImagerPlacementSettingsViewController::OnRecenterClicked() {
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        if (!Imager::RecenterPlacement(pid)) {
            ClearSelection(this);
            return;
        }

        Imager::PlacementInfo info;
        if (Imager::TryGetPlacementInfo(pid, info)) Populate(this, info);
    }

    void ImagerPlacementSettingsViewController::OnDeletePlacementClicked() {
        auto pid = GetPlacementId(this);
        if (pid.empty()) return;
        Imager::RemovePlacement(pid);
        HidePlacementModals(this);
        ClearSelection(this);
    }

    void ImagerPlacementSettingsViewController::OnCloseClicked() {
        HidePlacementModals(this);
        ClearSelection(this);
    }
}
