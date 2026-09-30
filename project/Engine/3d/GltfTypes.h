#pragma once
#include "Structs.h"
#include <vector>
#include <string>
#include <map>
#include <cstdint>

// GltfLoaderがglTFファイルから読み取る中間データ群。
// フェーズ3（Skeleton/Animation/SkinCluster/SkinnedObject3d）がこのデータを元に
// ジョイント階層の構築・アニメーション再生・スキン行列の計算を行う（今回のフェーズ2では未実装）。
//
// 対応範囲は1メッシュ・1プリミティブ・1スキン・1アニメーションのみ（GltfLoader.h参照）。

// glTFノード（Vector3/Quaternion/Matrix4x4はEngine\base\Structs.hのものをそのまま使う）
struct GltfNode {
    std::string name;
    Vector3 translate = { 0.0f, 0.0f, 0.0f };
    Quaternion rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    std::vector<int32_t> children;
    int32_t parent = -1;
};

// glTFスキン（jointNodeIndices[i]に対応する逆バインドポーズ行列がinverseBindMatrices[i]）
struct GltfSkin {
    std::vector<int32_t> jointNodeIndices;
    std::vector<Matrix4x4> inverseBindMatrices;
};

struct Vector3Key {
    float time;
    Vector3 value;
};

struct QuaternionKey {
    float time;
    Quaternion value;
};

// 1ノード分のアニメーションチャンネル（translation/scale/rotationのみ。LINEAR補間のみ対応）
struct GltfNodeAnimation {
    std::vector<Vector3Key> translate;
    std::vector<Vector3Key> scale;
    std::vector<QuaternionKey> rotate;
};

// GltfLoader::LoadGltfFile()の戻り値。glTFファイル1つ分の中間データをまとめたもの。
struct GltfModelData {
    std::vector<VertexData> vertices;
    std::vector<GltfNode> nodes;
    int32_t rootNodeIndex = -1;
    GltfSkin skin;
    std::map<std::string, GltfNodeAnimation> nodeAnimations; // ノード名キー
    float animationDuration = 0.0f;
    std::string textureFilePath;
};
