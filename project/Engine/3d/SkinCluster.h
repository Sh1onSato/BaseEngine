#pragma once
#include "Structs.h"
#include "DirectXCommon.h"
#include "GltfTypes.h"
#include "Skeleton.h"
#include <wrl.h>
#include <vector>

// スキン行列パレット（GPUのStructuredBuffer<float4x4> gSkinMatrices(t2)へ渡す実体）を管理するクラス。
// パレットの並び:
//   [0, 骨の数)                        … スキンの骨。逆バインド行列 * 骨のノードの今の行列
//   [骨の数, 骨の数 + 動かないメッシュの数) … 骨で動かないメッシュのノード。レストポーズの逆行列 * ノードの今の行列
//     （頂点はレストポーズの位置を焼き込んであるので、ノードがアニメーションで動いた分だけ動く）
// paletteMatrix[i] = inverseMatrices[i] * joints[paletteNodes[i]].skeletonSpaceMatrix を毎フレーム計算して書き込む。
class SkinCluster {
public:
    // data の骨と、骨で動かないメッシュのノードからパレットを作り、CPU書き込み可能なGPUリソースとして確保しMapする。
    void Initialize(DirectXCommon* dxCommon, const GltfModelData& data);

    // Skeletonの現在のskeletonSpaceMatrixを使ってスキン行列パレットを更新する。
    void Update(const Skeleton& skeleton);

    ID3D12Resource* GetPaletteResource() const { return paletteResource.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    Matrix4x4* paletteData = nullptr;
    // パレットの i 番目に使うノード番号と、掛ける逆行列
    std::vector<int32_t> paletteNodes;
    std::vector<Matrix4x4> inverseMatrices;
};
