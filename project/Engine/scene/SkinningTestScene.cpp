#include "SkinningTestScene.h"
#include "Framework.h"
#include "CameraManager.h"
#include "ModelManager.h"
#include "Logger.h"

using namespace Logger;

std::vector<VertexData> SkinningTestScene::BuildFloorVertices() const {
    // 影を受けるための床（非スキニング。boneWeights/boneIndicesはVertexDataのデフォルト値={1,0,0,0}/{0,0,0,0}のまま
    // ＝常にボーン0(=Object3dCommonのデフォルト単位行列パレット)を参照するので、実質何も変形しない）
    constexpr float kFloorHalfSize = 3.0f;
    constexpr float kFloorY = -0.1f;

    Vector4 posBL = { -kFloorHalfSize, kFloorY, -kFloorHalfSize, 1.0f };
    Vector4 posBR = {  kFloorHalfSize, kFloorY, -kFloorHalfSize, 1.0f };
    Vector4 posTR = {  kFloorHalfSize, kFloorY,  kFloorHalfSize, 1.0f };
    Vector4 posTL = { -kFloorHalfSize, kFloorY,  kFloorHalfSize, 1.0f };
    Vector3 up = { 0.0f, 1.0f, 0.0f };

    std::vector<VertexData> vertices;
    vertices.push_back(VertexData{ posBL, { 0.0f, 1.0f }, up });
    vertices.push_back(VertexData{ posBR, { 1.0f, 1.0f }, up });
    vertices.push_back(VertexData{ posTR, { 1.0f, 0.0f }, up });

    vertices.push_back(VertexData{ posBL, { 0.0f, 1.0f }, up });
    vertices.push_back(VertexData{ posTR, { 1.0f, 0.0f }, up });
    vertices.push_back(VertexData{ posTL, { 0.0f, 0.0f }, up });

    return vertices;
}

void SkinningTestScene::Initialize() {
    Framework* framework = Framework::GetInstance();
    DirectXCommon* dxCommon = framework->GetDxCommon();
    Object3dCommon* object3dCommon = framework->GetObject3dCommon();

    // --- カメラ ---
    CameraManager::GetInstance()->Initialize();
    auto newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate({ 0.0f, 1.5f, -5.0f });
    camera->SetRotate({ 0.15f, 0.0f, 0.0f }); // 少し見下ろす
    camera->Update();
    CameraManager::GetInstance()->AddCamera("default", std::move(newCamera));
    CameraManager::GetInstance()->SetActiveCamera("default");

    // --- glTFモデル読み込み(2ジョイントの棒。joint1のRotationアニメーション1本、t=0→0.5→1.0でループ) ---
    // ModelManagerがGltfModelDataの所有権を持つキャッシュなので、ここでは非所有ポインタとして受け取るだけでよい
    gltfData = ModelManager::GetInstance()->LoadGltfModel("Resources/gltf", "TestArm_embedded.gltf");

    // テスト用glTFにはテクスチャ画像が無いため textureFilePath は空文字列になる。
    // InitializeFromVertices()はTextureManager::LoadTexture()を呼ばない（プロシージャル生成用のため）ので、
    // 事前にロード済みの"white"テクスチャへフォールバックする（床メッシュと同じ扱い）。
    std::string barTexturePath = gltfData->textureFilePath.empty() ? "white" : gltfData->textureFilePath;

    barModel = std::make_unique<Model>();
    barModel->InitializeFromVertices(dxCommon, gltfData->vertices, barTexturePath);

    barObject = std::make_unique<SkinnedObject3d>();
    barObject->Initialize(object3dCommon);
    barObject->SetModel(barModel.get());
    barObject->SetGltfData(dxCommon, gltfData);
    barObject->SetLooping(true);

    // --- 床(非スキニング。デフォルトパレットのフォールバック確認を兼ねる) ---
    floorModel = std::make_unique<Model>();
    floorModel->InitializeFromVertices(dxCommon, BuildFloorVertices(), "white");

    floorObject = std::make_unique<Object3d>();
    floorObject->Initialize(object3dCommon);
    floorObject->SetModel(floorModel.get());
    // SetBonePalette()は呼ばない → Object3dCommonのデフォルト単位行列パレットにフォールバックする
}

void SkinningTestScene::Finalize() {
    CameraManager::GetInstance()->Initialize();
    camera = nullptr;
    gltfData = nullptr; // ModelManagerのキャッシュが所有しているため、ここでは解放しない
}

void SkinningTestScene::Update() {
    if (camera) {
        camera->Update();
    }

    barObject->Update();
    floorObject->Update();
}

void SkinningTestScene::Draw() {
    Framework* framework = Framework::GetInstance();
    DirectXCommon* dxCommon = framework->GetDxCommon();
    Object3dCommon* object3dCommon = framework->GetObject3dCommon();
    auto commandList = dxCommon->GetCommandList();

    // --- シャドウパス（ライト視点の深度のみ描画） ---
    object3dCommon->PreDrawShadow();
    Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    barObject->DrawShadow(lightViewProjection);
    floorObject->DrawShadow(lightViewProjection);
    object3dCommon->PostDrawShadow();

    // PostDrawShadow()はビューポート/シザーのみ復元するため、メインのレンダーターゲット(RTV/DSV)は
    // ここで明示的に張り直す必要がある（dxCommon->PreDraw()で最初に設定されたものと同じ場所に戻す）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetRtvHandle(dxCommon->GetBackBufferIndex());
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // --- 通常の描画パス ---
    object3dCommon->PreDraw();
    barObject->Draw();
    floorObject->Draw();
}
