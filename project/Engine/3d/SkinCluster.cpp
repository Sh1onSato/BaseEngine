#include "SkinCluster.h"
#include "Calculation.h"
#include "GltfLoader.h"
#include "Logger.h"
#include <algorithm>
#include <cassert>

using namespace Logger;

void SkinCluster::Initialize(DirectXCommon* dxCommon, const GltfModelData& data) {
    paletteNodes.clear();
    inverseMatrices.clear();

    // スキンの骨（頂点の JOINTS_0 がこの番号を指す）
    const size_t jointCount = (std::min)(data.skin.jointNodeIndices.size(), data.skin.inverseBindMatrices.size());
    for (size_t i = 0; i < jointCount; ++i) {
        paletteNodes.push_back(data.skin.jointNodeIndices[i]);
        inverseMatrices.push_back(data.skin.inverseBindMatrices[i]);
    }
    // 骨で動かないメッシュのノード（Model::InitializeFromGltf が頂点に 骨の数 + rigidSlot を入れる）
    for (int32_t nodeIndex : data.rigidNodeIndices) {
        paletteNodes.push_back(nodeIndex);
        inverseMatrices.push_back(Calculation::Inverse(GltfLoader::ComputeNodeWorldMatrix(data.nodes, nodeIndex)));
    }

    // 1つも無くても空のバッファは作れないので、最低1つ分は確保する
    const size_t paletteCount = (std::max)(paletteNodes.size(), size_t{ 1 });
    paletteResource = dxCommon->CreatBufferResource(sizeof(Matrix4x4) * paletteCount);

    HRESULT hr = paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&paletteData));
    if (FAILED(hr)) {
        CheckHResult(hr, "SkinCluster: Map paletteResource");
    }

    // Update()が呼ばれるまでの間、暴れた変形にならないよう単位行列で初期化しておく
    for (size_t i = 0; i < paletteCount; ++i) {
        paletteData[i] = Calculation::MakeIdentity4x4();
    }
}

void SkinCluster::Update(const Skeleton& skeleton) {
    if (!paletteData) { return; }

    const std::vector<Joint>& joints = skeleton.GetJoints();
    for (size_t i = 0; i < paletteNodes.size(); ++i) {
        const int32_t nodeIndex = paletteNodes[i];
        if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= joints.size()) { continue; }
        // 行ベクトル規約（local * parentWorldの合成順）に合わせ、逆行列を左から掛ける
        paletteData[i] = inverseMatrices[i] * joints[static_cast<size_t>(nodeIndex)].skeletonSpaceMatrix;
    }
}
