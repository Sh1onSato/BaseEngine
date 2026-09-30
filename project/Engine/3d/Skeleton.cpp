#include "Skeleton.h"
#include <unordered_map>

Skeleton Skeleton::Create(const GltfModelData& gltfData) {
    Skeleton skeleton;
    const std::vector<int32_t>& jointNodeIndices = gltfData.skin.jointNodeIndices;
    int32_t jointCount = static_cast<int32_t>(jointNodeIndices.size());
    skeleton.joints.resize(jointCount);

    // ノードインデックス -> joint配列インデックスの逆引きマップ
    // （jointNodeIndices=[1,2]なら { 1:0, 2:1 } となり、「ノード1がjoint0、ノード2がjoint1」という対応が引ける）
    std::unordered_map<int32_t, int32_t> nodeIndexToJointIndex;
    for (int32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        nodeIndexToJointIndex[jointNodeIndices[jointIndex]] = jointIndex;
    }

    for (int32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        int32_t nodeIndex = jointNodeIndices[jointIndex];
        const GltfNode& node = gltfData.nodes[nodeIndex];

        Joint& joint = skeleton.joints[jointIndex];
        joint.name = node.name;
        joint.transform.scale = node.scale;
        joint.transform.translate = node.translate;
        joint.rotate = node.rotate;
        joint.index = jointIndex;

        // 親ノードインデックス(gltfData.nodesベース)をjoint配列インデックスに変換する。
        // 親がスキンに含まれない（jointNodeIndicesに存在しない）場合はルート扱いにする。
        if (node.parent >= 0) {
            auto it = nodeIndexToJointIndex.find(node.parent);
            if (it != nodeIndexToJointIndex.end()) {
                joint.parent = it->second;
            }
        }

        // 子ノードインデックスのうち、スキンに含まれるものだけをjoint配列インデックスに変換して登録する
        for (int32_t childNodeIndex : node.children) {
            auto it = nodeIndexToJointIndex.find(childNodeIndex);
            if (it != nodeIndexToJointIndex.end()) {
                joint.children.push_back(it->second);
            }
        }
    }

    // parentを持たないjointをルートとする（単一ルートのスキンを想定。最初に見つかったものを採用）
    skeleton.rootIndex = 0;
    for (int32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        if (!skeleton.joints[jointIndex].parent.has_value()) {
            skeleton.rootIndex = jointIndex;
            break;
        }
    }

    skeleton.Update();
    return skeleton;
}

void Skeleton::Update() {
    if (joints.empty()) { return; }
    UpdateJointRecursive(rootIndex, nullptr);
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
