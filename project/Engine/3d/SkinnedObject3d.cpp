#include "SkinnedObject3d.h"
#include "Calculation.h"
#include <cmath>

void SkinnedObject3d::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
}

void SkinnedObject3d::SetGltfData(DirectXCommon* dxCommon, const GltfModelData* gltfData) {
    this->gltfData = gltfData;
    skeleton = Skeleton::Create(*gltfData);
    animation = Animation::Create(*gltfData);
    skinCluster.Initialize(dxCommon, gltfData->skin.inverseBindMatrices);
}

void SkinnedObject3d::Update() {
    if (gltfData) {
        animationTime += Calculation::kFixedDeltaTime;
        if (isLooping && animation.GetDuration() > 0.0f) {
            animationTime = std::fmod(animationTime, animation.GetDuration());
        }

        for (Joint& joint : skeleton.GetJointsMutable()) {
            joint.transform.translate = animation.SampleTranslate(joint.name, animationTime);
            joint.rotate              = animation.SampleRotate(joint.name, animationTime);
            joint.transform.scale     = animation.SampleScale(joint.name, animationTime);
        }

        skeleton.Update();
        skinCluster.Update(skeleton);
        SetBonePalette(skinCluster.GetPaletteResource());
    }

    Object3d::Update();
}
