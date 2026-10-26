#pragma once
#include "Structs.h"
#include "Calculation.h"
#include "GltfTypes.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

// スケルタルアニメーション用ジョイント（glTF のノード1個分）。
// 骨だけでなく、骨の親のノード（Z上→Y上 の回転など）や、骨で動かないメッシュを置いたノード（トラックの車輪など）も入る。
struct Joint {
    std::string name;
    Transform transform;                                              // ローカルTRSのうちscale/translateを使う
    Quaternion rotate = Calculation::IdentityQuaternion();            // ローカル回転（クォータニオン）
    Matrix4x4 localMatrix = Calculation::MakeIdentity4x4();           // ローカルTRSから計算したアフィン行列
    Matrix4x4 skeletonSpaceMatrix = Calculation::MakeIdentity4x4();   // モデルの原点を基準としたワールド行列
    int32_t index = 0;                                                // glTF のノード番号（joints の添字と同じ）
    std::optional<int32_t> parent;
    std::vector<int32_t> children;
};

// glTFのノード階層からジョイント階層を構築し、毎フレームのローカル→モデル空間の行列を更新するクラス。
class Skeleton {
public:
    // gltfData.nodes の並び順（ノード番号 = joints の添字）で Joint 配列を作る。姿勢はレストポーズ
    static Skeleton Create(const GltfModelData& gltfData);

    // 親の無いJointから深さ優先で辿り、各Jointのlocal/skeletonSpaceMatrixを再計算する。
    void Update();

    const std::vector<Joint>& GetJoints() const { return joints; }
    std::vector<Joint>& GetJointsMutable() { return joints; }
    // 名前のジョイントの番号（無ければ -1。同じ名前が複数あれば最初の物）
    int32_t FindJoint(const std::string& name) const;

private:
    // jointIndexのJointを計算し、その子を再帰的に辿る。
    void UpdateJointRecursive(int32_t jointIndex, const Matrix4x4* parentSkeletonSpaceMatrix);

    std::vector<Joint> joints;
    // 親を持たないジョイント（根元。複数あることもある）
    std::vector<int32_t> rootIndices;
};
