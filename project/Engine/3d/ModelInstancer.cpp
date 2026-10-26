#include "ModelInstancer.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Framework.h"
#include "Logger.h"
#include "Model.h"
#include "Object3dCommon.h"
#include <algorithm>
#include <cstring>

namespace {
    // Object3d と同じ回転順（オイラー角 → クォータニオン）でワールド行列を作る（Object3d::SetRotate と同じ合成順）
    Matrix4x4 MakeWorldMatrix(const Transform& transform) {
        Quaternion qX = Calculation::MakeAxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, transform.rotate.x);
        Quaternion qY = Calculation::MakeAxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, transform.rotate.y);
        Quaternion qZ = Calculation::MakeAxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, transform.rotate.z);
        Quaternion rotation = Calculation::Multiply(qZ, Calculation::Multiply(qY, qX));
        return Calculation::MakeAffineMatrix(transform.scale, rotation, transform.translate);
    }
}

ModelInstancer::ModelInstancer() = default;
ModelInstancer::~ModelInstancer() = default;

void ModelInstancer::Initialize(std::shared_ptr<Model> newModel, size_t initialCapacity) {
    Initialize(newModel.get(), initialCapacity);
    SetModel(std::move(newModel));
}

void ModelInstancer::Initialize(Model* newModel, size_t initialCapacity) {
    common = Framework::GetInstance()->GetObject3dCommon();
    SetModel(newModel);
    DirectXCommon* dxCommon = common->GetDxCommon();

    viewProjectionResource = dxCommon->CreatBufferResource(sizeof(Matrix4x4));
    Logger::CheckHResult(viewProjectionResource->Map(0, nullptr, reinterpret_cast<void**>(&viewProjectionData)),
                         "ModelInstancer: Map viewProjectionResource");
    *viewProjectionData = Calculation::MakeIdentity4x4();

    lightViewProjectionResource = dxCommon->CreatBufferResource(sizeof(Matrix4x4));
    Logger::CheckHResult(lightViewProjectionResource->Map(0, nullptr, reinterpret_cast<void**>(&lightViewProjectionData)),
                         "ModelInstancer: Map lightViewProjectionResource");
    *lightViewProjectionData = Calculation::MakeIdentity4x4();

    materialResource = dxCommon->CreatBufferResource(sizeof(Material));
    Logger::CheckHResult(materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData)),
                         "ModelInstancer: Map materialResource");
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = 1;
    materialData->uvTransform = Calculation::MakeIdentity4x4();

    instances.reserve(initialCapacity);
    CreateInstanceBuffer((std::max)(initialCapacity, size_t(1)));
}

void ModelInstancer::Clear() {
    instances.clear();
    isDirty = true;
    // 前のフレームの描画はもう終わっている（毎フレーム GPU の完了を待つ作り）ので、古いバッファを解放してよい
    retiredResources.clear();
}

void ModelInstancer::Add(const Transform& transform, const Vector4& color) {
    Add(MakeWorldMatrix(transform), color);
}

void ModelInstancer::Add(const Matrix4x4& world, const Vector4& color) {
    instances.push_back({ world, color });
    isDirty = true;
}

void ModelInstancer::CreateInstanceBuffer(size_t count) {
    if (instanceResource) {
        // このフレームですでに描画命令が古いバッファを参照しているかもしれないので、次の Clear まで残す
        retiredResources.push_back(instanceResource);
    }
    instanceResource = common->GetDxCommon()->CreatBufferResource(sizeof(InstanceData) * count);
    Logger::CheckHResult(instanceResource->Map(0, nullptr, reinterpret_cast<void**>(&instanceData)),
                         "ModelInstancer: Map instanceResource");
    capacity = count;
    isDirty = true;
}

void ModelInstancer::UploadInstances() {
    if (!isDirty) {
        return;
    }
    if (instances.size() > capacity) {
        // 足りなくなったら倍に広げる（毎フレーム少しずつ増えても作り直しが頻発しないように）
        CreateInstanceBuffer((std::max)(instances.size(), capacity * 2));
    }
    std::memcpy(instanceData, instances.data(), sizeof(InstanceData) * instances.size());
    isDirty = false;
}

void ModelInstancer::Draw() {
    if (!model || instances.empty()) {
        return;
    }
    Camera* currentCamera = camera ? camera : CameraManager::GetInstance()->GetActiveCamera();
    *viewProjectionData = currentCamera ? currentCamera->GetViewProjectionMatrix() : Calculation::MakeIdentity4x4();

    auto commandList = common->GetDxCommon()->GetCommandList();
    // マテリアル (b0)。ライト・影は Object3dCommon::PreDraw でセット済み
    commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
    // ビュー × プロジェクション (b1) とインスタンスデータ (t3)
    DrawWithInstancedPipeline(1, viewProjectionResource->GetGPUVirtualAddress(), Object3dCommon::kRootIndexInstances);
}

void ModelInstancer::DrawShadow() {
    if (!model || instances.empty()) {
        return;
    }
    *lightViewProjectionData = common->GetLightViewProjection();
    // ライト視点のビュー × プロジェクション (b0) とインスタンスデータ (t3)
    DrawWithInstancedPipeline(0, lightViewProjectionResource->GetGPUVirtualAddress(), Object3dCommon::kShadowRootIndexInstances);
}

void ModelInstancer::DrawWithInstancedPipeline(UINT viewProjectionRootIndex, D3D12_GPU_VIRTUAL_ADDRESS viewProjectionAddress,
                                               UINT instancesRootIndex) {
    UploadInstances();
    auto commandList = common->GetDxCommon()->GetCommandList();
    common->SetInstancedPipeline();
    commandList->SetGraphicsRootConstantBufferView(viewProjectionRootIndex, viewProjectionAddress);
    commandList->SetGraphicsRootShaderResourceView(instancesRootIndex, instanceResource->GetGPUVirtualAddress());

    const UINT count = static_cast<UINT>(instances.size());
    if (common->IsShadowPass()) {
        // 影用ルートシグネチャにはテクスチャの枠が無いので Model::Draw は使えない（Object3d::DrawShadow と同じ）
        commandList->IASetVertexBuffers(0, 1, &model->GetVertexBufferView());
        commandList->DrawInstanced(model->GetVertexCount(), count, 0, 0);
    } else {
        model->Draw(commandList, count);
    }
    // 後に続く Object3d がそのまま描けるよう、元の PSO に戻す
    common->RestorePipeline();
}
