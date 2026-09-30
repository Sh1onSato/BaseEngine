#include "Skybox.h"
#include "TextureManager.h"
#include "CameraManager.h"

void Skybox::Initialize(SkyboxCommon* common, const std::string& textureFilePath) {
    this->common = common;

    // テクスチャの読み込み
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
    textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath);

    // 定数バッファの作成
    wvpResource = common->GetDxCommon()->CreatBufferResource(sizeof(TransformationMatrix));
    wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

    wvpData->World = Calculation::MakeIdentity4x4();
    wvpData->WVP = Calculation::MakeIdentity4x4();

    transform = { {500.0f, 500.0f, 500.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
}

void Skybox::Update() {
    // カメラの座標を取得して追従させる（無限遠に見せるため）
    Camera* currentCamera = camera;
    if (!currentCamera) {
        currentCamera = CameraManager::GetInstance()->GetActiveCamera();
    }

    if (currentCamera) {
        transform.translate = currentCamera->GetTransform().translate;
    }

    Matrix4x4 worldMatrix = Calculation::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    Matrix4x4 viewProjectionMatrix = currentCamera ? currentCamera->GetViewProjectionMatrix() : Calculation::MakeIdentity4x4();

    wvpData->World = worldMatrix;
    wvpData->WVP = worldMatrix * viewProjectionMatrix;
}

void Skybox::Draw() {
    if (!model) return;

    auto commandList = common->GetDxCommon()->GetCommandList();

    // 定数バッファのセット (b0)
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());
    // テクスチャのセット (t0)
    commandList->SetGraphicsRootDescriptorTable(1, TextureManager::GetInstance()->GetSrvHandleGPU(textureIndex));

    // モデルの頂点バッファをセットして描画
    D3D12_VERTEX_BUFFER_VIEW vbv = model->GetVertexBufferView();
    commandList->IASetVertexBuffers(0, 1, &vbv);
    commandList->DrawInstanced(model->GetVertexCount(), 1, 0, 0);
}
