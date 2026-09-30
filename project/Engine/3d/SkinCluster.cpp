#include "SkinCluster.h"
#include "Calculation.h"
#include "Logger.h"
#include <cassert>

using namespace Logger;

void SkinCluster::Initialize(DirectXCommon* dxCommon, const std::vector<Matrix4x4>& inverseBindMatrices) {
    this->inverseBindMatrices = inverseBindMatrices;

    size_t jointCount = inverseBindMatrices.size();
    paletteResource = dxCommon->CreatBufferResource(sizeof(Matrix4x4) * jointCount);

    HRESULT hr = paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&paletteData));
    if (FAILED(hr)) {
        Log("SkinCluster: paletteResource Map failed.\n");
        assert(false);
        return;
    }

    // Update()が呼ばれるまでの間、暴れた変形にならないよう単位行列で初期化しておく
    for (size_t i = 0; i < jointCount; ++i) {
        paletteData[i] = Calculation::MakeIdentity4x4();
    }
}

void SkinCluster::Update(const Skeleton& skeleton) {
    if (!paletteData) { return; }

    const std::vector<Joint>& joints = skeleton.GetJoints();
    size_t jointCount = inverseBindMatrices.size();
    for (size_t i = 0; i < jointCount && i < joints.size(); ++i) {
        // 行ベクトル規約（local * parentWorldの合成順）に合わせ、逆バインドポーズ行列を左から掛ける
        paletteData[i] = inverseBindMatrices[i] * joints[i].skeletonSpaceMatrix;
    }
}
