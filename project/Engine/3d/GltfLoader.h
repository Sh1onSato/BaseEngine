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
// 対応範囲（スコープクリープ防止のため厳密に絞っている）:
//   - .gltf（テキストJSON） + 外部.bin または base64データURI(data:application/octet-stream;base64,...)
//   - 1メッシュ・1プリミティブ・1スキン・1アニメーションのみ（複数あればインデックス0のみ使用）
//   - 頂点属性: POSITION / NORMAL / TEXCOORD_0 / JOINTS_0 / WEIGHTS_0 のみ
//   - アニメーションチャンネル: translation / rotation / scale のみ（weightsアニメーション等は非対応）
//   - 補間方法: LINEARのみ対応（STEP/CUBICSPLINEのデータが来てもLINEARとして読み、ログ警告のみ出す）
// 非対応:
//   - .glbバイナリ形式
//   - morph target
//   - ノードのmatrixプロパティ（TRS(translation/rotation/scale)のみ対応）
//   - スキン無しのアクセサsparse
class GltfLoader {
public:
    GltfLoader() = delete;

    // directoryPath/filename の .gltf を読み込み、CPU側の中間データに変換して返す。
    // 失敗時（ファイルが開けない、JSON構文エラー等）はstd::runtime_errorを投げる。
    static GltfModelData LoadGltfFile(const std::string& directoryPath, const std::string& filename);

private:
    // gltf.buffers[bufferIndex] を読み込む。
    // 外部.binファイル参照と、base64データURI(data:...;base64,...)の両方に対応する。
    static std::vector<uint8_t> LoadBufferData(const JsonValue& gltf, int32_t bufferIndex, const std::string& directoryPath);

    // 以下はLoadGltfFile()を50行以内に収めるための分割ヘルパー（呼び出し順に並べている）
    static std::vector<GltfNode> ParseNodes(const JsonValue& gltf);
    static int32_t FindRootNodeIndex(const JsonValue& gltf);
    static std::vector<VertexData> ParseVertices(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers);
    static GltfSkin ParseSkin(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers);
    static std::map<std::string, GltfNodeAnimation> ParseAnimations(
        const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers,
        const std::vector<GltfNode>& nodes, float& animationDuration);
    static std::string ParseTextureFilePath(const JsonValue& gltf, const std::string& directoryPath);

    // accessor -> bufferView -> buffer の3段参照をたどり、componentType/typeに応じてバイト列から
    // 数値を取り出しdoubleの配列として返す（要素はaccessorのtypeが持つコンポーネント数だけ連続で並ぶ）。
    // sparseアクセサは非対応。
    static std::vector<double> ReadAccessorRaw(const JsonValue& gltf, int32_t accessorIndex, const std::vector<std::vector<uint8_t>>& buffers);

    // glTFの列優先・列ベクトル規約の4x4行列16要素を、本エンジンの行優先・行ベクトル規約のMatrix4x4へ
    // 転置して変換する（skin.inverseBindMatrices専用。ノードのTRSはMakeAffineMatrix側で規約を合わせるため変換不要）。
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
