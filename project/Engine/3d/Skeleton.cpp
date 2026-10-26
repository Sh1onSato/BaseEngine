#include "Skeleton.h"

Skeleton Skeleton::Create(const GltfModelData& gltfData) {
    Skeleton skeleton;
    const int32_t nodeCount = static_cast<int32_t>(gltfData.nodes.size());
    skeleton.joints.resize(nodeCount);

    // ノード番号をそのままジョイント番号にする（骨の番号への変換は SkinCluster が jointNodeIndices で行う）
    for (int32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        const GltfNode& node = gltfData.nodes[nodeIndex];
        Joint& joint = skeleton.joints[nodeIndex];
        joint.name = node.name;
        joint.transform.scale = node.scale;
        joint.transform.translate = node.translate;
        joint.rotate = node.rotate;
        joint.index = nodeIndex;
        if (node.parent >= 0 && node.parent < nodeCount) {
            joint.parent = node.parent;
        } else {
            skeleton.rootIndices.push_back(nodeIndex);
        }
        for (int32_t childIndex : node.children) {
            if (childIndex >= 0 && childIndex < nodeCount) {
                joint.children.push_back(childIndex);
            }
        }
    }

    skeleton.Update();
    return skeleton;
}

void Skeleton::Update() {
    for (int32_t rootIndex : rootIndices) {
        UpdateJointRecursive(rootIndex, nullptr);
    }
}

int32_t Skeleton::FindJoint(const std::string& name) const {
    for (const Joint& joint : joints) {
        if (joint.name == name) {
            return joint.index;
        }
    }
    return -1;
}

void Skeleton::UpdateJointRecursive(int32_t jointIndex, const Matrix4x4* parentSkeletonSpaceMatrix) {
    Joint& joint = joints[jointIndex];

    // ローカルTRSからアフィン行列を計算（行ベクトル規約）
    joint.localMatrix = Calculation::MakeAffineMatrix(joint.transform.scale, joint.rotate, joint.transform.translate);

    // ワールド変換の合成は常に local * parentWorld の順（Object3d::Update()のworldMatrix * viewProjectionMatrixと同じ規約）
    if (parentSkeletonSpaceMatrix) {
        joint.skeletonSpaceMatrix = joint.localMatrix * (*parentSkeletonSpaceMatrix);
    } else {
        joint.skeletonSpaceMatrix = joint.localMatrix;
    }

    for (int32_t childIndex : joint.children) {
        UpdateJointRecursive(childIndex, &joint.skeletonSpaceMatrix);
    }
}
