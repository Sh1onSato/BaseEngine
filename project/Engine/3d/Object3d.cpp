#include "Object3d.h"
#include "externals/imgui/imgui.h"
#include "CameraManager.h"

void Object3d::Initialize(Object3dCommon* common) {
    this->common = common;

    // WVP用のリソース作成
    wvpResource = common->GetDxCommon()->CreatBufferResource(sizeof(TransformationMatrix));
    wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

    // マテリアル用のリソース作成
    materialResource = common->GetDxCommon()->CreatBufferResource(sizeof(Material));
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

    // シャドウマップ生成パス用のWVPリソース作成
    shadowWvpResource = common->GetDxCommon()->CreatBufferResource(sizeof(Matrix4x4));
    shadowWvpResource->Map(0, nullptr, reinterpret_cast<void**>(&shadowWvpData));
    *shadowWvpData = Calculation::MakeIdentity4x4();

    // 初期化
    wvpData->World = Calculation::MakeIdentity4x4();
    wvpData->WVP = Calculation::MakeIdentity4x4();

    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = 1;
    materialData->uvTransform = Calculation::MakeIdentity4x4();

    // デフォルトのトランスフォーム
    transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    quaternionRotation = Calculation::IdentityQuaternion();
}

void Object3d::SetRotate(const Vector3& rotate) {
    transform.rotate = rotate;
    // オイラー角からクォータニオンを作成
    // MakeAffineMatrix(Vector3)側のオイラー回転順(RotX*RotY*RotZ)と一致させるため、
    // クォータニオン積はその逆順(qZ, qY, qXの順にMultiply)で合成する
    Quaternion qX = Calculation::MakeAxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, rotate.x);
    Quaternion qY = Calculation::MakeAxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, rotate.y);
    Quaternion qZ = Calculation::MakeAxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, rotate.z);
    quaternionRotation = Calculation::Multiply(qZ, Calculation::Multiply(qY, qX));
}

void Object3d::Update() {
    // 1. ワールド行列の計算 (クォータニオンを使用)
    worldMatrix = Calculation::MakeAffineMatrix(transform.scale, quaternionRotation, transform.translate);

    // 2. ビュー・プロジェクション行列の取得
    Matrix4x4 viewProjectionMatrix;
    Camera* currentCamera = camera;
    if (!currentCamera) {
        currentCamera = CameraManager::GetInstance()->GetActiveCamera();
    }

    if (currentCamera) {
        viewProjectionMatrix = currentCamera->GetViewProjectionMatrix();
    } else {
        viewProjectionMatrix = Calculation::MakeIdentity4x4();
    }

    Matrix4x4 wvpMatrix = worldMatrix * viewProjectionMatrix;
    // 3. データ転送
    wvpData->World = worldMatrix;
    wvpData->WVP = wvpMatrix;
}

void Object3d::Draw() {
    if (!model) { return; } // モデルがセットされていなければ何もしない

    auto commandList = common->GetDxCommon()->GetCommandList();

    // マテリアルバッファをセット (b0)
    commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

    // WVPバッファをセット (b1)
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

    // スキニング用ボーン行列パレットをセット (t2、ルートSRV)
    // 未設定ならObject3dCommon側のデフォルト単位行列パレットにフォールバックする
    D3D12_GPU_VIRTUAL_ADDRESS paletteAddress = bonePaletteResource ? bonePaletteResource->GetGPUVirtualAddress() : common->GetDefaultBonePaletteGPUAddress();
    commandList->SetGraphicsRootShaderResourceView(8, paletteAddress);

    // モデル自身の描画処理
    model->Draw(commandList);
}

void Object3d::DrawShadow(const Matrix4x4& lightViewProjection) {
    if (!model) { return; } // モデルがセットされていなければ何もしない

    auto commandList = common->GetDxCommon()->GetCommandList();

    // ライト視点のWVP行列を計算して転送
    *shadowWvpData = worldMatrix * lightViewProjection;

    // WVPバッファをセット (b0、シャドウ用ルートシグネチャの構成は[0]=b0, [1]=t2)
    commandList->SetGraphicsRootConstantBufferView(0, shadowWvpResource->GetGPUVirtualAddress());

    // スキニング用ボーン行列パレットをセット (t2、ルートSRV、index1)
    // ここを忘れると影だけバインドポーズのまま焼き付くので注意
    D3D12_GPU_VIRTUAL_ADDRESS shadowPaletteAddress = bonePaletteResource ? bonePaletteResource->GetGPUVirtualAddress() : common->GetDefaultBonePaletteGPUAddress();
    commandList->SetGraphicsRootShaderResourceView(1, shadowPaletteAddress);

    // 深度のみの描画（Model::Drawはテクスチャ用のディスクリプタテーブルをb2にセットするため、
    // シャドウ用ルートシグネチャ(b0のみ)では使えない。頂点バッファのバインドとDrawInstancedのみ直接発行する）
    commandList->IASetVertexBuffers(0, 1, &model->GetVertexBufferView());
    commandList->DrawInstanced(model->GetVertexCount(), 1, 0, 0);
}

void Object3d::ImGui() {
    if (ImGui::BeginTabBar("Object3dTabBar")) {
        if (ImGui::BeginTabItem("Transform")) {
            ImGui::DragFloat3("Position", &transform.translate.x, 0.1f);
            if (ImGui::DragFloat3("Rotation (Euler)", &transform.rotate.x, 0.01f)) {
                SetRotate(transform.rotate); // オイラー角を更新したらクォータニオンも更新
            }
            ImGui::DragFloat4("Quaternion", &quaternionRotation.x, 0.01f);
            if (ImGui::Button("Normalize Quaternion")) {
                quaternionRotation = Calculation::Normalize(quaternionRotation);
            }
            ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Material")) {
            ImGui::ColorEdit4("Color", &materialData->color.x);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Camera")) {
            if (camera) {
                ImGui::DragFloat3("Camera Position", &camera->GetTransform().translate.x, 0.1f);
                ImGui::DragFloat3("Camera Rotation", &camera->GetTransform().rotate.x, 0.01f);
            } else {
                ImGui::Text("Camera is not set.");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}