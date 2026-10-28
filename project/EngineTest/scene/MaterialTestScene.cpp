#include "MaterialTestScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "PostProcess.h"
#include "PrimitiveMesh.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include <cmath>
#include <format>

namespace {
    const char* kCameraName = "MaterialTest";
    constexpr int kUiLayer = 100;
    const char* kNormalMapPath = "Resources/textures/test_normal_bumps.png";
    const char* kMetallicRoughnessPath = "Resources/textures/test_metallic_roughness.png";

    const Vector3 kCameraPosition = { 0.0f, 3.5f, -13.0f };
    const Vector3 kCameraRotate = { 0.15f, 0.0f, 0.0f };

    // 左の球の並び
    constexpr int kGridColumns = 5;   // 金属らしさ 0, 0.25, 0.5, 0.75, 1
    constexpr int kGridRows = 3;      // 粗さ
    const float kGridRoughness[kGridRows] = { 0.1f, 0.5f, 0.9f };
    const Vector3 kGridOrigin = { -8.0f, 4.6f, 0.0f }; // 左上の球
    constexpr float kGridSpacing = 1.4f;
    constexpr float kGridSphereSize = 1.1f;
    const Vector4 kGridColor = { 1.0f, 0.78f, 0.35f, 1.0f }; // 金色っぽい

    // 回る点光源
    const Vector3 kLightCenter = { -2.0f, 3.0f, -3.0f };
    constexpr float kLightRadius = 6.0f;
    constexpr float kLightSpeed = 0.7f;
    constexpr float kRotateSpeed = 0.5f;
}

MaterialTestScene::MaterialTestScene() = default;
// Object3d・Model の中身はヘッダーで見えないので、デストラクタは .cpp に置く
MaterialTestScene::~MaterialTestScene() = default;

void MaterialTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();
    SetupSphereGrid();
    SetupNormalMapObjects();
    SetupOtherObjects();

    // 回る点光源（照り返しが動いて見える）
    PointLight& pointLight = Framework::GetInstance()->GetObject3dCommon()->GetPointLightData();
    pointLight.intensity = 1.5f;
    pointLight.radius = 16.0f;
    pointLight.decay = 1.0f;
    pointLight.color = { 1.0f, 0.95f, 0.85f, 1.0f };
    // 光る球の光がにじむように、画面の効果のブルームを付ける（シーンを切り替えると戻る）
    PostEffectSettings& settings = Framework::GetInstance()->GetPostProcess()->GetSettings();
    settings.bloomEnabled = true;
    settings.bloomThreshold = 1.0f; // 1 を超える明るさ（光る球）だけにじませる
    settings.bloomIntensity = 1.0f;
    // 物が床に接している所・すき間を暗くする（浮いて見えないように）
    settings.ambientOcclusionEnabled = true;
}

void MaterialTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate(kCameraPosition);
    camera->SetRotate(kCameraRotate);
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void MaterialTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("材質テスト (MATERIAL_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("左の球: 横 = 金属らしさ 0 → 1、縦 = 粗さ 0.1 / 0.5 / 0.9   右: 法線マップの板・立方体・球 / 金属・粗さの画像の立方体（市松模様: ざらざら ⇔ つるつるの金属）/ 光る球\n"
                      "[N] 法線マップ ON/OFF  [P] 金属・粗さを使う当たり方 ON/OFF（OFF = 今までの簡単な当たり方）  [O] 接している所を暗くする（SSAO）ON/OFF");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 18.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);
}

std::unique_ptr<Object3d> MaterialTestScene::CreateObject(std::shared_ptr<Model> model, const Vector3& position, const Vector3& scale,
                                                          const Vector4& color) {
    std::unique_ptr<Object3d> object = std::make_unique<Object3d>();
    object->Initialize(Framework::GetInstance()->GetObject3dCommon());
    object->SetModel(std::move(model));
    object->SetTranslate(position);
    object->SetScale(scale);
    object->SetColor(color);
    return object;
}

void MaterialTestScene::SetupSphereGrid() {
    // 同じ球のモデル（共有）を、物ごとの上書き（SetMetallic / SetRoughness）で変える
    std::shared_ptr<Model> sphere = ModelManager::GetInstance()->GetSphereModel();
    for (int row = 0; row < kGridRows; ++row) {
        for (int column = 0; column < kGridColumns; ++column) {
            const Vector3 position = kGridOrigin + Vector3{ kGridSpacing * static_cast<float>(column), -kGridSpacing * static_cast<float>(row), 0.0f };
            std::unique_ptr<Object3d> object =
                CreateObject(sphere, position, { kGridSphereSize, kGridSphereSize, kGridSphereSize }, kGridColor);
            object->SetMetallic(static_cast<float>(column) / static_cast<float>(kGridColumns - 1));
            object->SetRoughness(kGridRoughness[row]);
            objects.push_back(std::move(object));
        }
    }
}

void MaterialTestScene::SetupNormalMapObjects() {
    DirectXCommon* dxCommon = Framework::GetInstance()->GetDxCommon();
    // 法線マップは「モデル」に付ける（共有のモデルに付けると他の立方体・球まで変わるので、自分で作る）
    normalCubeModel = std::make_shared<Model>();
    normalCubeModel->InitializeFromVertices(dxCommon, PrimitiveMesh::BuildCube());
    normalCubeModel->SetNormalMap(kNormalMapPath);
    normalCubeModel->SetRoughness(0.35f);
    normalSphereModel = std::make_shared<Model>();
    normalSphereModel->InitializeFromVertices(dxCommon, PrimitiveMesh::BuildSphere(32, 24));
    normalSphereModel->SetNormalMap(kNormalMapPath, 0.6f);
    normalSphereModel->SetRoughness(0.3f);

    // 正面を向いた板（法線マップの凹凸が一番分かりやすい）
    objects.push_back(CreateObject(normalCubeModel, { 2.0f, 3.2f, 0.5f }, { 3.0f, 3.0f, 0.2f }, { 0.75f, 0.75f, 0.8f, 1.0f }));
    std::unique_ptr<Object3d> cube = CreateObject(normalCubeModel, { 5.5f, 3.6f, 0.0f }, { 1.6f, 1.6f, 1.6f }, { 0.9f, 0.5f, 0.4f, 1.0f });
    rotatingCube = cube.get();
    objects.push_back(std::move(cube));
    std::unique_ptr<Object3d> sphere = CreateObject(normalSphereModel, { 8.0f, 3.6f, 0.0f }, { 1.8f, 1.8f, 1.8f }, { 0.4f, 0.7f, 1.0f, 1.0f });
    rotatingSphere = sphere.get();
    objects.push_back(std::move(sphere));
}

void MaterialTestScene::SetupOtherObjects() {
    DirectXCommon* dxCommon = Framework::GetInstance()->GetDxCommon();
    // 金属・粗さの画像（市松模様: ざらざらで金属でない マス と つるつるの金属 のマスが交互）
    metallicRoughnessCubeModel = std::make_shared<Model>();
    metallicRoughnessCubeModel->InitializeFromVertices(dxCommon, PrimitiveMesh::BuildCube());
    metallicRoughnessCubeModel->SetMetallic(1.0f);
    metallicRoughnessCubeModel->SetRoughness(1.0f);
    metallicRoughnessCubeModel->SetMetallicRoughnessMap(kMetallicRoughnessPath);
    objects.push_back(CreateObject(metallicRoughnessCubeModel, { 3.0f, 0.8f, -1.5f }, { 1.6f, 1.6f, 1.6f }, { 0.9f, 0.9f, 0.95f, 1.0f }));

    // 光る球（物ごとの発光。強さ 1 を超えるとブルームでにじむ）
    std::unique_ptr<Object3d> glow = CreateObject(ModelManager::GetInstance()->GetSphereModel(), { 6.5f, 0.6f, -1.5f }, { 1.2f, 1.2f, 1.2f },
                                                  { 0.2f, 0.1f, 0.05f, 1.0f });
    glow->SetEmissive({ 1.0f, 0.5f, 0.15f }, 4.0f);
    objects.push_back(std::move(glow));

    // 床（今までの簡単な当たり方のまま）
    objects.push_back(CreateObject(ModelManager::GetInstance()->GetCubeModel(), { 0.0f, -0.1f, 2.0f }, { 24.0f, 0.2f, 12.0f },
                                   { 0.5f, 0.5f, 0.55f, 1.0f }));
}

void MaterialTestScene::Finalize() {
    // 点光源を元に戻す（既定は無効）
    Framework::GetInstance()->GetObject3dCommon()->GetPointLightData().intensity = 0.0f;
    objects.clear();
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void MaterialTestScene::Update() {
    UpdateInput();
    elapsedTime += Time::GetDeltaTime();

    const float angle = elapsedTime * kLightSpeed;
    Framework::GetInstance()->GetObject3dCommon()->GetPointLightData().position =
        kLightCenter + Vector3{ std::sin(angle) * kLightRadius, 0.0f, std::cos(angle) * kLightRadius * 0.5f };
    rotatingCube->SetRotate({ 0.4f, elapsedTime * kRotateSpeed, 0.0f });
    rotatingSphere->SetRotate({ 0.0f, elapsedTime * kRotateSpeed, 0.0f });

    camera->Update();
    for (std::unique_ptr<Object3d>& object : objects) {
        object->Update();
    }

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 98.0f }));
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void MaterialTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_N)) {
        isNormalMapOn = !isNormalMapOn;
        normalCubeModel->GetSurfaceMaterial(0).normalStrength = isNormalMapOn ? 1.0f : 0.0f;
        normalSphereModel->GetSurfaceMaterial(0).normalStrength = isNormalMapOn ? 0.6f : 0.0f;
    }
    if (input->TriggerKey(DIK_O)) {
        PostEffectSettings& settings = Framework::GetInstance()->GetPostProcess()->GetSettings();
        settings.ambientOcclusionEnabled = !settings.ambientOcclusionEnabled;
    }
    if (input->TriggerKey(DIK_P)) {
        isPhysicalOn = !isPhysicalOn;
        // 床（最後に作った物）は今までの当たり方のまま
        for (size_t i = 0; i + 1 < objects.size(); ++i) {
            objects[i]->SetPhysicalLighting(isPhysicalOn);
        }
    }
}

void MaterialTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    for (std::unique_ptr<Object3d>& object : objects) {
        object->Draw();
    }

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void MaterialTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    for (std::unique_ptr<Object3d>& object : objects) {
        object->DrawShadow(lightViewProjection);
    }
    object3dCommon->PostDrawShadow();
}

std::string MaterialTestScene::BuildStatusText() const {
    const bool isAmbientOcclusionOn = Framework::GetInstance()->GetPostProcess()->GetSettings().ambientOcclusionEnabled;
    return std::format("法線マップ {}  金属・粗さを使う当たり方 {}  SSAO {}", isNormalMapOn ? "ON" : "OFF", isPhysicalOn ? "ON" : "OFF（今までの当たり方）",
                       isAmbientOcclusionOn ? "ON" : "OFF");
}
