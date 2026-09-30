#pragma once
#include "Structs.h"
#include "Calculation.h"
#include "GltfTypes.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

// スケルタルアニメーション用ジョイント（骨）1個分のデータ。
// nameはGltfModelData::nodeAnimationsのキーと突き合わせるために保持する。
struct Joint {
    std::string name;
    Transform transform;                                              // ローカルTRSのうちscale/translateを使う
    Quaternion rotate = Calculation::IdentityQuaternion();            // ローカル回転（クォータニオン）
    Matrix4x4 localMatrix = Calculation::MakeIdentity4x4();           // ローカルTRSから計算したアフィン行列
    Matrix4x4 skeletonSpaceMatrix = Calculation::MakeIdentity4x4();   // スケルトンルートを基準としたワールド行列
    int32_t index = 0;
    std::optional<int32_t> parent;
    std::vector<int32_t> children;
};

// glTFのスキン情報からジョイント階層を構築し、毎フレームのローカル→スケルトン空間行列更新を行うクラス。
class Skeleton {
public:
    // gltfData.skin.jointNodeIndices の並び順でJoint配列を構築する。
    // この並び順は、GltfLoaderが頂点のJOINTS_0(boneIndices)をそのまま格納しているため、
    // 頂点シェーダー側が参照するボーン番号と完全に一致していなければならない。
    static Skeleton Create(const GltfModelData& gltfData);

    // ルートJointから深さ優先で辿り、各Jointのlocal/skeletonSpaceMatrixを再計算する。
    void Update();

    const std::vector<Joint>& GetJoints() const { return joints; }
    std::vector<Joint>& GetJointsMutable() { return joints; }

private:
    // jointIndexのJointを計算し、その子を再帰的に辿る。
    // jointNodeIndicesの並び順が親→子の順とは限らないため、配列の先頭から単純ループはせず、
    // 必ずルートから深さ優先でたどる。
    void UpdateJointRecursive(int32_t jointIndex, const Matrix4x4* parentSkeletonSpaceMatrix);

    std::vector<Joint> joints;
    int32_t rootIndex = 0;
};
