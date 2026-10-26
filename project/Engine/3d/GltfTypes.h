#pragma once
#include "Structs.h"
#include <vector>
#include <string>
#include <map>
#include <cstdint>

// GltfLoaderがglTFファイルから読み取る中間データ群。
// Skeleton/Animation/SkinCluster/SkinnedObject3d がこのデータを元に
// ジョイント階層の構築・アニメーション再生・スキン行列の計算を行う。
//
// 座標はすべてエンジンの座標系（左手系）に直してある（glTF は右手系なので Z を反転し、三角形の向きも直している）。
// 対応範囲は GltfLoader.h を参照。

// glTFノード（Vector3/Quaternion/Matrix4x4はEngine\base\Structs.hのものをそのまま使う）
// "matrix" で書かれたノードも、読み込み時に translate / rotate / scale へ分解してある
struct GltfNode {
    std::string name;
    Vector3 translate = { 0.0f, 0.0f, 0.0f };
    Quaternion rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    std::vector<int32_t> children;
    int32_t parent = -1;
    // このノードが表示するメッシュ・使うスキン（-1 ならなし）
    int32_t mesh = -1;
    int32_t skin = -1;
    // モーフターゲットの最初の重み（ノードの "weights"、無ければメッシュの "weights"。モーフが無ければ空）
    std::vector<float> morphWeights;
};

// モーフターゲット1つ分（表情・口パクなど）。頂点ごとに「重み 1 のときにずらす量」を持つ（GltfPrimitive::vertices と同じ並び）
struct GltfMorphTarget {
    std::vector<Vector3> positionDeltas;
    // 空なら法線は変えない
    std::vector<Vector3> normalDeltas;
};

// 1つのマテリアルで描く三角形の集まり（glTF のプリミティブ1つ分）。
// スキンの無いメッシュは、ノードの位置・回転・大きさ（レストポーズ）を頂点に焼き込んである
struct GltfPrimitive {
    std::vector<VertexData> vertices;
    // materials の番号（-1 ならマテリアルなし = 白）
    int32_t materialIndex = -1;
    // 骨で動くメッシュか（true なら頂点はメッシュの元の位置のままで、骨の行列で動かす）
    bool isSkinned = false;
    // 骨で動かないメッシュのとき、置かれているノードの GltfModelData::rigidNodeIndices での番号（骨で動くなら -1）。
    // SkinnedObject3d で動かすときに、このノードのアニメーションで動かすために使う
    int32_t rigidSlot = -1;
    // このプリミティブを置いているノードの番号（モーフの重みはノードごと）
    int32_t nodeIndex = -1;
    // モーフターゲット（無ければ空）。スキンの無いメッシュはノードの行列で向きを直してある
    std::vector<GltfMorphTarget> morphTargets;
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

// キーとキーの間の補間のしかた（CUBICSPLINE は接線を使わず kLinear として読む）
enum class GltfInterpolation {
    kLinear, // なめらかにつなぐ
    kStep,   // 次のキーまで前のキーの値のまま（パラパラ漫画のような動き）
};

// 1ノード分のアニメーションチャンネル（translation/scale/rotationのみ）
struct GltfNodeAnimation {
    std::vector<Vector3Key> translate;
    std::vector<Vector3Key> scale;
    std::vector<QuaternionKey> rotate;
    GltfInterpolation translateInterpolation = GltfInterpolation::kLinear;
    GltfInterpolation scaleInterpolation = GltfInterpolation::kLinear;
    GltfInterpolation rotateInterpolation = GltfInterpolation::kLinear;
};

// 1ノード分のモーフの重みのアニメーション（キーごとに、ターゲットの数だけ重みが並ぶ）
struct GltfWeightsAnimation {
    std::vector<float> times;
    std::vector<std::vector<float>> weights;
    GltfInterpolation interpolation = GltfInterpolation::kLinear;
};

// アニメーション1本（歩く・走るなど）
struct GltfAnimation {
    // 名前が無ければ "Animation<番号>"
    std::string name;
    // 一番遅いキーの時刻（秒）
    float duration = 0.0f;
    // ノード番号キー（動かないノードは入っていない）
    std::map<int32_t, GltfNodeAnimation> nodeAnimations;
    // モーフの重みのアニメーション（ノード番号キー）
    std::map<int32_t, GltfWeightsAnimation> weightsAnimations;
};

// glTF の画像1枚。filePath（外部ファイル）か data（ファイルの中に入っている png・jpg の中身）のどちらかが入る
struct GltfImage {
    // TextureManager に登録する名前（外部ファイルならそのパス、中に入っている画像なら "<glTFのパス>#image<番号>"）
    std::string name;
    std::string filePath;
    std::vector<uint8_t> data;
};

// glTF のマテリアル（今は基本色だけ。光沢・法線マップなどは読まない）
struct GltfMaterial {
    std::string name;
    // 基本色（画像が無いときはこの色で塗る。画像があるときは画像だけを使い、この色は掛けない）
    Vector4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    // images の番号（-1 なら画像なし）
    int32_t imageIndex = -1;
};

// GltfLoader::LoadGltfFile()の戻り値。glTFファイル1つ分の中間データをまとめたもの。
struct GltfModelData {
    // シーンに置かれたすべてのメッシュのプリミティブ（同じメッシュを2つのノードが使えば2回入る）
    std::vector<GltfPrimitive> primitives;
    std::vector<GltfNode> nodes;
    // 骨で動かないメッシュを置いているノード（GltfPrimitive::rigidSlot の番号順）
    std::vector<int32_t> rigidNodeIndices;
    GltfSkin skin;
    std::vector<GltfAnimation> animations;
    // モーフターゲットの名前（メッシュの extras.targetNames。Blender のシェイプキー名。無ければ空）
    std::vector<std::string> morphTargetNames;
    std::vector<GltfImage> images;
    std::vector<GltfMaterial> materials;
};
