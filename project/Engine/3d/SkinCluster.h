#pragma once
#include "Structs.h"
#include "DirectXCommon.h"
#include "Skeleton.h"
#include <wrl.h>
#include <vector>

// スキン行列パレット（GPUのStructuredBuffer<float4x4> gSkinMatrices(t2)へ渡す実体）を管理するクラス。
// skinMatrix[i] = inverseBindMatrices[i] * joints[i].skeletonSpaceMatrix を毎フレーム計算して書き込む。
class SkinCluster {
public:
    // inverseBindMatrices.size() 個ぶんのMatrix4x4をCPU書き込み可能なGPUリソースとして確保しMapする。
    void Initialize(DirectXCommon* dxCommon, const std::vector<Matrix4x4>& inverseBindMatrices);

    // Skeletonの現在のskeletonSpaceMatrixを使ってスキン行列パレットを更新する。
    void Update(const Skeleton& skeleton);

    ID3D12Resource* GetPaletteResource() const { return paletteResource.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    Matrix4x4* paletteData = nullptr;
    std::vector<Matrix4x4> inverseBindMatrices;
};
