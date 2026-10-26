#pragma once
#include "GltfTypes.h"
#include "JsonValue.h"
#include <string>
#include <vector>
#include <array>
#include <cstdint>

// 自前glTFローダー（Model::LoadObjFileと同じ「自前で読む」スタイル）。
// nlohmann/json等の外部ライブラリは使わず、Engine\base\JsonParserのみでJSONを読む。
//
// 対応範囲:
//   - .gltf（テキストJSON） + 外部.bin または base64データURI、.glb（1つにまとまったバイナリ形式）
//   - シーンに置かれたすべてのメッシュ・プリミティブ（三角形のみ）とすべてのアニメーション。スキンは1つのみ
//     （2つ目以降のスキンを使うメッシュは読み飛ばす）
//   - 頂点属性: POSITION / NORMAL / TEXCOORD_0 / JOINTS_0 / WEIGHTS_0（NORMAL が無ければ面の向きから作る）。
//     整数で保存された値（normalized）は 0〜1 に直して読む
//   - ノードの translation / rotation / scale と matrix
//   - モーフターゲット（POSITION / NORMAL のずらす量。TANGENT は読まない）と、メッシュ・ノードの "weights"
//   - アニメーションチャンネル: translation / rotation / scale / weights。補間は LINEAR と STEP
//     （CUBICSPLINE は接線を使わず LINEAR として読む）
//   - 画像: 外部ファイル・base64データURI・.glb の中の画像。マテリアルは基本色（画像か色）だけ
// 座標系: glTF（右手系）をエンジン（左手系）に直す。Z を反転し、三角形の頂点の順番を逆にする
//   （Blender の向きのまま表示され、LevelLoader の Blender → エンジン変換とも合う）。
//   - sparse アクセサ（変化のある要素だけを書く形式。Blender のシェイプキーでよく使われる）
// 非対応:
//   - 法線マップ・光沢などのマテリアル（基本色だけ）
//   - 2つ目以降のスキン
class GltfLoader {
public:
    GltfLoader() = delete;

    // directoryPath/filename の .gltf / .glb を読み込み、CPU側の中間データに変換して返す。
    // 失敗時（ファイルが開けない、JSON構文エラー等）はstd::runtime_errorを投げる。
    static GltfModelData LoadGltfFile(const std::string& directoryPath, const std::string& filename);

    // materialIndex のマテリアルの画像を TextureManager に読み込み、その名前を返す（Model の textureFilePath に渡す）。
    // 画像が無ければ基本色の 1x1 の画像を作る。マテリアルが無ければ "white"
    static std::string LoadMaterialTexture(const GltfModelData& data, int32_t materialIndex);

    // ノードのレストポーズのワールド行列（親をたどって掛ける）。nodeIndex が -1 なら単位行列
    static Matrix4x4 ComputeNodeWorldMatrix(const std::vector<GltfNode>& nodes, int32_t nodeIndex);

    // 骨で動く頂点を、骨がレストポーズのときの形に CPU で動かして返す（骨の重みは外す）。
    // スキンのあるモデルを Object3d で動かさずに表示するとき用
    static std::vector<VertexData> BakeRestPose(const GltfModelData& data, const std::vector<VertexData>& vertices);

    // モーフターゲットを重みで混ぜた頂点を outVertices（baseVertices と同じ数）に書く。
    // 位置 = 元の位置 + Σ 重み × ずらす量（法線も同じように混ぜて長さ1に直す）
    static void ApplyMorphTargets(const std::vector<VertexData>& baseVertices, const std::vector<GltfMorphTarget>& targets,
                                  const std::vector<float>& weights, VertexData* outVertices);

private:
    // gltf.buffers[bufferIndex] を読み込む。
    // 外部.binファイル参照と、base64データURI(data:...;base64,...)、.glb の BIN チャンク（uri なし）に対応する。
    static std::vector<uint8_t> LoadBufferData(const JsonValue& gltf, int32_t bufferIndex, const std::string& directoryPath,
                                               const std::vector<uint8_t>& glbBinChunk);
    // .glb を JSON 部分と BIN 部分に分ける
    static void SplitGlb(const std::vector<uint8_t>& fileBytes, const std::string& filePath,
                         std::string& jsonText, std::vector<uint8_t>& binChunk);

    // 以下はLoadGltfFile()を50行以内に収めるための分割ヘルパー（呼び出し順に並べている）
    static std::vector<GltfNode> ParseNodes(const JsonValue& gltf);
    // シーンのルートからたどれるノードが表示するメッシュを、すべて primitives に入れる
    static void ParseMeshes(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, GltfModelData& result);
    // プリミティブ1つ分の頂点を読む（三角形でなければ頂点なしで返す）
    static GltfPrimitive ParsePrimitive(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, const JsonValue& primitive);
    static GltfSkin ParseSkin(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers);
    static std::vector<GltfAnimation> ParseAnimations(
        const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, size_t nodeCount);
    // animations[animationIndex] を1本読む
    static GltfAnimation ParseAnimation(
        const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, size_t nodeCount, size_t animationIndex);
    static std::vector<GltfImage> ParseImages(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers,
                                              const std::string& directoryPath, const std::string& filePath);
    static std::vector<GltfMaterial> ParseMaterials(const JsonValue& gltf);

    // accessor -> bufferView -> buffer の3段参照をたどり、componentType/typeに応じてバイト列から
    // 数値を取り出しdoubleの配列として返す（要素はaccessorのtypeが持つコンポーネント数だけ連続で並ぶ）。
    // "normalized" な整数は 0〜1（符号付きは -1〜1）に直す。sparse（一部の要素だけ差し替える）にも対応。
    static std::vector<double> ReadAccessorRaw(const JsonValue& gltf, int32_t accessorIndex, const std::vector<std::vector<uint8_t>>& buffers);

    // glTFの列優先・列ベクトル規約の4x4行列16要素を、本エンジンの行優先・行ベクトル規約のMatrix4x4へ
    // 転置して変換する（座標系の変換はしない）。
    static Matrix4x4 ConvertGltfMatrix(const std::array<float, 16>& m);

    // componentType(5120=BYTE,5121=UBYTE,5122=SHORT,5123=USHORT,5125=UINT,5126=FLOAT)のバイトサイズ
    static size_t ComponentByteSize(int32_t componentType);
    // accessorのtype文字列("SCALAR","VEC2","VEC3","VEC4","MAT4")が持つコンポーネント数
    static size_t TypeComponentCount(const std::string& type);
    // componentTypeに応じてbuffer[byteOffset]から1コンポーネント分の値を読み取りdoubleへ変換する
    static double ReadComponentValue(const std::vector<uint8_t>& buffer, size_t byteOffset, int32_t componentType);

    // base64文字列（データURIの","以降の部分）をデコードする
    static std::vector<uint8_t> DecodeBase64(const std::string& base64);
};
