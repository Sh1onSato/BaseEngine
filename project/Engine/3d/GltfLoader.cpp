#include "GltfLoader.h"
#include "Calculation.h"
#include "JsonParser.h"
#include "Logger.h"
#include "TextureManager.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>

namespace {
    // .glb の先頭とチャンクの種類（リトルエンディアンの4文字）
    constexpr uint32_t kGlbMagic = 0x46546C67;      // "glTF"
    constexpr uint32_t kGlbChunkJson = 0x4E4F534A;  // "JSON"
    constexpr uint32_t kGlbChunkBin = 0x004E4942;   // "BIN\0"

    [[noreturn]] void Fail(const std::string& message) {
        Logger::Log(message);
        throw std::runtime_error(message);
    }

    // --- glTF（右手系）→ エンジン（左手系）。Z を反転する ---
    // （Blender → glTF は (x, z, -y)、LevelLoader の Blender → エンジンは (x, z, y) なので、Z の反転で両者がそろう）
    Vector3 ConvertPosition(const Vector3& v) {
        return { v.x, v.y, -v.z };
    }
    // Z で鏡に映した回転（回転軸の X・Y が逆になる）
    Quaternion ConvertRotation(const Quaternion& q) {
        return { -q.x, -q.y, q.z, q.w };
    }
    // 行列を Z で鏡に映す（S * M * S、S = Z だけ -1）。行と列のどちらか一方だけが Z(=2) の要素の符号が変わる
    Matrix4x4 ConvertMatrixHandedness(const Matrix4x4& m) {
        Matrix4x4 result = m;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                if ((row == 2) != (col == 2)) {
                    result.m[row][col] = -result.m[row][col];
                }
            }
        }
        return result;
    }

    // エンジンの並び（行ベクトル、S * R * T）の行列を translate / rotate / scale に分ける（せん断は無い前提）
    void DecomposeMatrix(const Matrix4x4& m, Vector3& translate, Quaternion& rotate, Vector3& scale) {
        translate = { m.m[3][0], m.m[3][1], m.m[3][2] };
        float rows[3][3]{};
        float scales[3]{};
        for (int i = 0; i < 3; ++i) {
            scales[i] = std::sqrt(m.m[i][0] * m.m[i][0] + m.m[i][1] * m.m[i][1] + m.m[i][2] * m.m[i][2]);
            for (int j = 0; j < 3; ++j) {
                rows[i][j] = scales[i] > 0.0f ? m.m[i][j] / scales[i] : 0.0f;
            }
        }
        scale = { scales[0], scales[1], scales[2] };

        // rows は Calculation::MakeRotateMatrix と同じ並び（一般的な回転行列 R の転置）なので R(i, j) = rows[j][i]
        auto r = [&rows](int i, int j) { return rows[j][i]; };
        const float trace = r(0, 0) + r(1, 1) + r(2, 2);
        if (trace > 0.0f) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            rotate = { (r(2, 1) - r(1, 2)) / s, (r(0, 2) - r(2, 0)) / s, (r(1, 0) - r(0, 1)) / s, 0.25f * s };
        } else if (r(0, 0) > r(1, 1) && r(0, 0) > r(2, 2)) {
            float s = std::sqrt(1.0f + r(0, 0) - r(1, 1) - r(2, 2)) * 2.0f;
            rotate = { 0.25f * s, (r(0, 1) + r(1, 0)) / s, (r(0, 2) + r(2, 0)) / s, (r(2, 1) - r(1, 2)) / s };
        } else if (r(1, 1) > r(2, 2)) {
            float s = std::sqrt(1.0f + r(1, 1) - r(0, 0) - r(2, 2)) * 2.0f;
            rotate = { (r(0, 1) + r(1, 0)) / s, 0.25f * s, (r(1, 2) + r(2, 1)) / s, (r(0, 2) - r(2, 0)) / s };
        } else {
            float s = std::sqrt(1.0f + r(2, 2) - r(0, 0) - r(1, 1)) * 2.0f;
            rotate = { (r(0, 2) + r(2, 0)) / s, (r(1, 2) + r(2, 1)) / s, 0.25f * s, (r(1, 0) - r(0, 1)) / s };
        }
    }

    uint32_t ReadUint32(const std::vector<uint8_t>& bytes, size_t offset) {
        if (offset + sizeof(uint32_t) > bytes.size()) {
            Fail("GltfLoader: .glb のヘッダーが途中で切れています");
        }
        uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    std::vector<uint8_t> ReadFileBytes(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            Fail("GltfLoader: ファイルを開けません: " + filePath);
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
            Fail("GltfLoader: ファイルの読み込みに失敗しました: " + filePath);
        }
        return data;
    }

    // URI の %XX（スペースなどの記号を表す書き方）を元の文字に戻す
    std::string DecodeUri(const std::string& uri) {
        std::string result;
        result.reserve(uri.size());
        for (size_t i = 0; i < uri.size(); ++i) {
            if (uri[i] == '%' && i + 2 < uri.size() &&
                std::isxdigit(static_cast<unsigned char>(uri[i + 1])) && std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
                result.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16)));
                i += 2;
            } else {
                result.push_back(uri[i]);
            }
        }
        return result;
    }

    // NORMAL が無いモデル用に、面の向きから頂点の法線を作る（同じ頂点を使う面の向きを平均する）
    std::vector<double> ComputeSmoothNormals(const std::vector<double>& positions, const std::vector<uint32_t>& indices) {
        const size_t vertexCount = positions.size() / 3;
        std::vector<double> normals(positions.size(), 0.0);
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const uint32_t i0 = indices[t];
            const uint32_t i1 = indices[t + 1];
            const uint32_t i2 = indices[t + 2];
            if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
                continue;
            }
            double e1[3]{};
            double e2[3]{};
            for (size_t c = 0; c < 3; ++c) {
                e1[c] = positions[i1 * 3 + c] - positions[i0 * 3 + c];
                e2[c] = positions[i2 * 3 + c] - positions[i0 * 3 + c];
            }
            // glTF は反時計回りが表なので e1 × e2 が外向き（大きい面ほど強く効く）
            const double cross[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            for (uint32_t index : { i0, i1, i2 }) {
                for (size_t c = 0; c < 3; ++c) {
                    normals[index * 3 + c] += cross[c];
                }
            }
        }
        for (size_t v = 0; v < vertexCount; ++v) {
            double* n = &normals[v * 3];
            const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length > 0.0) {
                n[0] /= length;
                n[1] /= length;
                n[2] /= length;
            } else {
                n[1] = 1.0;
            }
        }
        return normals;
    }

    // 行列の回転・大きさの部分（3x3）の行列式。マイナスなら鏡に映す行列
    float Determinant3x3(const Matrix4x4& m) {
        return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1])
             - m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0])
             + m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
    }

    // 向き（法線など）を行列の回転・大きさの部分だけで動かす（行ベクトルなので n * M）
    Vector3 TransformDirection(const Vector3& n, const Matrix4x4& m) {
        return { n.x * m.m[0][0] + n.y * m.m[1][0] + n.z * m.m[2][0],
                 n.x * m.m[0][1] + n.y * m.m[1][1] + n.z * m.m[2][1],
                 n.x * m.m[0][2] + n.y * m.m[1][2] + n.z * m.m[2][2] };
    }

    // 三角形ごとに2番目と3番目を入れ替える（表裏を逆にする）
    template <typename T>
    void SwapTriangleCorners(std::vector<T>& values) {
        for (size_t t = 0; t + 2 < values.size(); t += 3) {
            std::swap(values[t + 1], values[t + 2]);
        }
    }

    // ノードの行列でプリミティブの頂点（とモーフのずらす量）を動かし、骨の重みを外す（スキンの無いメッシュ用）。
    // 法線は逆行列の転置で動かす（大きさが軸ごとに違っても面に垂直なまま）。鏡に映す行列なら表裏が入れ替わるので三角形の順番も直す
    void TransformPrimitive(GltfPrimitive& primitive, const Matrix4x4& world) {
        const Matrix4x4 normalMatrix = Calculation::Transpose(Calculation::Inverse(world));
        for (VertexData& v : primitive.vertices) {
            Vector3 position = Calculation::Transform({ v.position.x, v.position.y, v.position.z }, world);
            v.position = { position.x, position.y, position.z, 1.0f };
            v.normal = Calculation::Normalize(TransformDirection(v.normal, normalMatrix));
            v.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
            std::fill(std::begin(v.boneIndices), std::end(v.boneIndices), 0u);
        }
        // ずらす量は向きと長さだけなので、平行移動は掛けない
        for (GltfMorphTarget& target : primitive.morphTargets) {
            for (Vector3& delta : target.positionDeltas) { delta = TransformDirection(delta, world); }
            for (Vector3& delta : target.normalDeltas) { delta = TransformDirection(delta, normalMatrix); }
        }
        if (Determinant3x3(world) < 0.0f) {
            SwapTriangleCorners(primitive.vertices);
            for (GltfMorphTarget& target : primitive.morphTargets) {
                SwapTriangleCorners(target.positionDeltas);
                SwapTriangleCorners(target.normalDeltas);
            }
        }
    }

    // JSON の数の配列を float の配列にする
    std::vector<float> ReadFloatArray(const JsonValue& array) {
        std::vector<float> values(array.Size());
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = (float)array[i].AsNumber();
        }
        return values;
    }

    // "normalized" な整数を 0〜1（符号付きは -1〜1）に直す
    double NormalizeComponent(double value, int32_t componentType) {
        switch (componentType) {
            case 5120: return (std::max)(value / 127.0, -1.0);   // BYTE
            case 5121: return value / 255.0;                     // UNSIGNED_BYTE
            case 5122: return (std::max)(value / 32767.0, -1.0); // SHORT
            case 5123: return value / 65535.0;                   // UNSIGNED_SHORT
            default: return value;
        }
    }
}

// ノード一覧をパースし、children配列からparentを逆算する
std::vector<GltfNode> GltfLoader::ParseNodes(const JsonValue& gltf) {
    std::vector<GltfNode> nodes;
    if (!gltf.Contains("nodes")) return nodes;

    const JsonValue& nodesJson = gltf["nodes"];
    nodes.resize(nodesJson.Size());
    for (size_t i = 0; i < nodesJson.Size(); ++i) {
        const JsonValue& nodeJson = nodesJson[i];
        GltfNode node;
        if (nodeJson.Contains("name")) node.name = nodeJson["name"].AsString();
        if (nodeJson.Contains("matrix")) {
            // matrix で書かれたノードは TRS に分ける（Skeleton・Animation は TRS で扱うため）
            const JsonValue& matrixJson = nodeJson["matrix"];
            std::array<float, 16> values{};
            for (size_t k = 0; k < values.size(); ++k) values[k] = (float)matrixJson[k].AsNumber();
            DecomposeMatrix(ConvertGltfMatrix(values), node.translate, node.rotate, node.scale);
        }
        if (nodeJson.Contains("translation")) {
            const JsonValue& t = nodeJson["translation"];
            node.translate = { (float)t[(size_t)0].AsNumber(), (float)t[(size_t)1].AsNumber(), (float)t[(size_t)2].AsNumber() };
        }
        if (nodeJson.Contains("rotation")) {
            const JsonValue& r = nodeJson["rotation"];
            node.rotate = { (float)r[(size_t)0].AsNumber(), (float)r[(size_t)1].AsNumber(), (float)r[(size_t)2].AsNumber(), (float)r[(size_t)3].AsNumber() };
        }
        if (nodeJson.Contains("scale")) {
            const JsonValue& s = nodeJson["scale"];
            node.scale = { (float)s[(size_t)0].AsNumber(), (float)s[(size_t)1].AsNumber(), (float)s[(size_t)2].AsNumber() };
        }
        node.translate = ConvertPosition(node.translate);
        node.rotate = ConvertRotation(node.rotate);
        if (nodeJson.Contains("mesh")) node.mesh = (int32_t)nodeJson["mesh"].AsNumber();
        if (nodeJson.Contains("skin")) node.skin = (int32_t)nodeJson["skin"].AsNumber();
        if (nodeJson.Contains("weights")) node.morphWeights = ReadFloatArray(nodeJson["weights"]);
        if (nodeJson.Contains("children")) {
            const JsonValue& childrenJson = nodeJson["children"];
            for (size_t c = 0; c < childrenJson.Size(); ++c) {
                node.children.push_back((int32_t)childrenJson[c].AsNumber());
            }
        }
        nodes[i] = node;
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        for (int32_t childIndex : nodes[i].children) {
            if (childIndex >= 0 && (size_t)childIndex < nodes.size()) {
                nodes[(size_t)childIndex].parent = (int32_t)i;
            }
        }
    }
    return nodes;
}

// シーンのルートから深さ優先でノードをたどり、メッシュを持つノードのプリミティブを全部読む
void GltfLoader::ParseMeshes(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, GltfModelData& result) {
    const JsonValue& meshesJson = gltf["meshes"];
    if (meshesJson.Size() == 0) return;
    std::vector<GltfNode>& nodes = result.nodes;

    // たどり始めるノード（シーンのルート。シーンが無ければ親の無いノード全部）
    std::vector<int32_t> stack;
    size_t sceneIndex = gltf.Contains("scene") ? (size_t)gltf["scene"].AsNumber() : 0;
    const JsonValue& sceneNodes = gltf["scenes"][sceneIndex]["nodes"];
    if (sceneNodes.Size() > 0) {
        for (size_t i = sceneNodes.Size(); i-- > 0;) stack.push_back((int32_t)sceneNodes[i].AsNumber());
    } else {
        for (size_t i = nodes.size(); i-- > 0;) {
            if (nodes[i].parent < 0) stack.push_back((int32_t)i);
        }
    }

    std::vector<bool> isVisited(nodes.size(), false);
    bool hasLoggedExtraSkin = false;
    while (!stack.empty()) {
        const int32_t nodeIndex = stack.back();
        stack.pop_back();
        if (nodeIndex < 0 || (size_t)nodeIndex >= nodes.size() || isVisited[(size_t)nodeIndex]) continue;
        isVisited[(size_t)nodeIndex] = true;
        GltfNode& node = nodes[(size_t)nodeIndex];
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) stack.push_back(*it);

        if (node.mesh < 0 || (size_t)node.mesh >= meshesJson.Size()) continue;
        if (node.skin > 0) {
            if (!hasLoggedExtraSkin) {
                Logger::Log("GltfLoader: 2つ目以降のスキンを使うメッシュは非対応のため読み飛ばしました");
                hasLoggedExtraSkin = true;
            }
            continue;
        }
        // 骨で動くメッシュはノードの位置を使わない（glTF の決まり。位置は骨の行列で決まる）
        const bool isSkinned = node.skin == 0;
        const Matrix4x4 world = ComputeNodeWorldMatrix(nodes, nodeIndex);
        // 骨で動かないメッシュは、置かれているノードを覚えておく（そのノードのアニメーションで動かせるように）
        int32_t rigidSlot = -1;
        if (!isSkinned) {
            rigidSlot = static_cast<int32_t>(result.rigidNodeIndices.size());
            result.rigidNodeIndices.push_back(nodeIndex);
        }
        const JsonValue& meshJson = meshesJson[(size_t)node.mesh];
        // モーフの最初の重み（ノードに無ければメッシュの物）とターゲットの名前（Blender のシェイプキー名）
        if (node.morphWeights.empty() && meshJson.Contains("weights")) {
            node.morphWeights = ReadFloatArray(meshJson["weights"]);
        }
        const JsonValue& targetNames = meshJson["extras"]["targetNames"];
        if (result.morphTargetNames.empty() && targetNames.Size() > 0) {
            for (size_t i = 0; i < targetNames.Size(); ++i) {
                result.morphTargetNames.push_back(targetNames[i].AsString());
            }
        }
        const JsonValue& primitivesJson = meshJson["primitives"];
        for (size_t p = 0; p < primitivesJson.Size(); ++p) {
            GltfPrimitive primitive = ParsePrimitive(gltf, buffers, primitivesJson[p]);
            if (primitive.vertices.empty()) continue;
            primitive.isSkinned = isSkinned;
            primitive.rigidSlot = rigidSlot;
            primitive.nodeIndex = nodeIndex;
            if (!isSkinned) {
                TransformPrimitive(primitive, world);
            }
            // 重みが書かれていなければ全部 0（モーフしない形）から始める
            if (node.morphWeights.size() < primitive.morphTargets.size()) {
                node.morphWeights.resize(primitive.morphTargets.size(), 0.0f);
            }
            result.primitives.push_back(std::move(primitive));
        }
    }
}

// プリミティブ1つ分の VertexData の配列とマテリアル番号を読む（indices指定があれば展開して非インデックス化する）
GltfPrimitive GltfLoader::ParsePrimitive(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, const JsonValue& primitive) {
    GltfPrimitive result;
    // mode 4 = 三角形（省略時も 4）。線・点は描けないので読まない
    if (primitive.Contains("mode") && (int32_t)primitive["mode"].AsNumber() != 4) {
        Logger::Log("GltfLoader: 三角形以外のプリミティブ(mode)は非対応のため読み飛ばしました");
        return result;
    }
    result.materialIndex = primitive.Contains("material") ? (int32_t)primitive["material"].AsNumber() : -1;

    const JsonValue& attributes = primitive["attributes"];
    auto readAttribute = [&](const char* name) {
        return attributes.Contains(name) ? ReadAccessorRaw(gltf, (int32_t)attributes[name].AsNumber(), buffers) : std::vector<double>();
    };
    std::vector<double> positions = readAttribute("POSITION");
    std::vector<double> normals = readAttribute("NORMAL");
    std::vector<double> texcoords = readAttribute("TEXCOORD_0");
    std::vector<double> joints = readAttribute("JOINTS_0");
    std::vector<double> weights = readAttribute("WEIGHTS_0");
    const size_t vertexCount = positions.size() / 3;

    std::vector<uint32_t> indices;
    if (primitive.Contains("indices")) {
        std::vector<double> raw = ReadAccessorRaw(gltf, (int32_t)primitive["indices"].AsNumber(), buffers);
        indices.reserve(raw.size());
        for (double value : raw) {
            indices.push_back(static_cast<uint32_t>(value));
        }
    } else {
        indices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) indices[i] = (uint32_t)i;
    }
    if (normals.empty()) {
        normals = ComputeSmoothNormals(positions, indices);
    }

    // モーフターゲット（ターゲットごとに、元の頂点の並びでずらす量が入っている）
    const JsonValue& targetsJson = primitive["targets"];
    std::vector<std::vector<double>> targetPositions(targetsJson.Size());
    std::vector<std::vector<double>> targetNormals(targetsJson.Size());
    for (size_t i = 0; i < targetsJson.Size(); ++i) {
        const JsonValue& target = targetsJson[i];
        if (target.Contains("POSITION")) targetPositions[i] = ReadAccessorRaw(gltf, (int32_t)target["POSITION"].AsNumber(), buffers);
        if (target.Contains("NORMAL")) targetNormals[i] = ReadAccessorRaw(gltf, (int32_t)target["NORMAL"].AsNumber(), buffers);
    }
    result.morphTargets.resize(targetsJson.Size());
    // idx 番目の頂点のずらす量（足りなければ 0）
    auto readDelta = [](const std::vector<double>& values, uint32_t idx) {
        if ((size_t)idx * 3 + 2 >= values.size()) return Vector3{ 0.0f, 0.0f, 0.0f };
        return ConvertPosition({ (float)values[idx * 3 + 0], (float)values[idx * 3 + 1], (float)values[idx * 3 + 2] });
    };

    // 右手系 → 左手系で表と裏が入れ替わるので、三角形の2番目と3番目の頂点を入れ替えて読む
    constexpr size_t kCornerOrder[3] = { 0, 2, 1 };
    result.vertices.reserve(indices.size());
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        for (size_t corner : kCornerOrder) {
            const uint32_t idx = indices[t + corner];
            if (idx >= vertexCount) {
                Fail("GltfLoader: 頂点番号(indices)が頂点数を超えています");
            }
            for (size_t i = 0; i < result.morphTargets.size(); ++i) {
                result.morphTargets[i].positionDeltas.push_back(readDelta(targetPositions[i], idx));
                if (!targetNormals[i].empty()) {
                    result.morphTargets[i].normalDeltas.push_back(readDelta(targetNormals[i], idx));
                }
            }
            VertexData v{};
            Vector3 position = ConvertPosition({ (float)positions[idx * 3 + 0], (float)positions[idx * 3 + 1], (float)positions[idx * 3 + 2] });
            v.position = { position.x, position.y, position.z, 1.0f };
            v.normal = ConvertPosition({ (float)normals[idx * 3 + 0], (float)normals[idx * 3 + 1], (float)normals[idx * 3 + 2] });
            // glTFのTEXCOORD_0は原点が左上でDirectXと同じ規約のため、.objローダーと違い反転不要
            v.texcoord = texcoords.empty()
                ? Vector2{ 0.0f, 0.0f }
                : Vector2{ (float)texcoords[idx * 2 + 0], (float)texcoords[idx * 2 + 1] };
            if (!weights.empty()) {
                float w[4] = { (float)weights[idx * 4 + 0], (float)weights[idx * 4 + 1], (float)weights[idx * 4 + 2], (float)weights[idx * 4 + 3] };
                // 合計がぴったり 1 でない書き出しもあるので、1 にそろえる
                const float sum = w[0] + w[1] + w[2] + w[3];
                const float invSum = sum > 0.0f ? 1.0f / sum : 0.0f;
                v.boneWeights = { w[0] * invSum, w[1] * invSum, w[2] * invSum, w[3] * invSum };
            }
            if (!joints.empty()) {
                for (size_t k = 0; k < 4; ++k) {
                    v.boneIndices[k] = (uint32_t)joints[idx * 4 + k];
                }
            }
            result.vertices.push_back(v);
        }
    }
    return result;
}

// skins[0] からjointNodeIndicesとinverseBindMatrices(転置・座標系変換済み)を読み取る
GltfSkin GltfLoader::ParseSkin(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers) {
    GltfSkin skin;
    if (!gltf.Contains("skins") || gltf["skins"].Size() == 0) return skin;
    const JsonValue& skinJson = gltf["skins"][(size_t)0];

    if (skinJson.Contains("joints")) {
        const JsonValue& jointsJson = skinJson["joints"];
        for (size_t i = 0; i < jointsJson.Size(); ++i) {
            skin.jointNodeIndices.push_back((int32_t)jointsJson[i].AsNumber());
        }
    }

    if (skinJson.Contains("inverseBindMatrices")) {
        std::vector<double> raw = ReadAccessorRaw(gltf, (int32_t)skinJson["inverseBindMatrices"].AsNumber(), buffers);
        size_t jointCount = raw.size() / 16;
        skin.inverseBindMatrices.reserve(jointCount);
        for (size_t i = 0; i < jointCount; ++i) {
            std::array<float, 16> m{};
            for (size_t k = 0; k < 16; ++k) m[k] = (float)raw[i * 16 + k];
            skin.inverseBindMatrices.push_back(ConvertMatrixHandedness(ConvertGltfMatrix(m)));
        }
    } else {
        // glTF仕様上、未指定時は単位行列がデフォルト
        Matrix4x4 identity{};
        for (int i = 0; i < 4; ++i) identity.m[i][i] = 1.0f;
        skin.inverseBindMatrices.assign(skin.jointNodeIndices.size(), identity);
    }
    return skin;
}

// すべてのアニメーションを読む
std::vector<GltfAnimation> GltfLoader::ParseAnimations(
    const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, size_t nodeCount) {
    std::vector<GltfAnimation> animations;
    const JsonValue& animationsJson = gltf["animations"];
    for (size_t i = 0; i < animationsJson.Size(); ++i) {
        animations.push_back(ParseAnimation(gltf, buffers, nodeCount, i));
    }
    return animations;
}

// animations[animationIndex] からノード番号キーのアニメーションチャンネル一式を読み取る
GltfAnimation GltfLoader::ParseAnimation(
    const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers, size_t nodeCount, size_t animationIndex) {

    GltfAnimation animation;
    const JsonValue& animationJson = gltf["animations"][animationIndex];
    animation.name = animationJson.Contains("name") ? animationJson["name"].AsString() : "";
    if (animation.name.empty()) {
        animation.name = "Animation" + std::to_string(animationIndex);
    }
    const JsonValue& samplersJson = animationJson["samplers"];
    const JsonValue& channelsJson = animationJson["channels"];
    bool hasLoggedCubicSpline = false;

    for (size_t i = 0; i < channelsJson.Size(); ++i) {
        const JsonValue& channel = channelsJson[i];
        const JsonValue& target = channel["target"];
        if (!target.Contains("node")) continue; // ノード対象外のチャンネルは非対応のためスキップ

        int32_t targetNodeIndex = (int32_t)target["node"].AsNumber();
        if (targetNodeIndex < 0 || (size_t)targetNodeIndex >= nodeCount) continue;
        std::string path = target["path"].AsString();
        if (path != "translation" && path != "rotation" && path != "scale" && path != "weights") {
            Logger::Log("GltfLoader: 未対応のアニメーションチャンネルpathをスキップしました: " + path);
            continue;
        }

        int32_t samplerIndex = (int32_t)channel["sampler"].AsNumber();
        const JsonValue& sampler = samplersJson[(size_t)samplerIndex];
        std::string interpolationName = sampler.Contains("interpolation") ? sampler["interpolation"].AsString() : "LINEAR";
        GltfInterpolation interpolation = interpolationName == "STEP" ? GltfInterpolation::kStep : GltfInterpolation::kLinear;
        // CUBICSPLINE は1キーに「入りの接線・値・出の接線」の3つが並ぶ。値だけを使って LINEAR として読む
        const bool isCubicSpline = interpolationName == "CUBICSPLINE";
        if (isCubicSpline && !hasLoggedCubicSpline) {
            Logger::Log("GltfLoader: CUBICSPLINE補間は接線を使わずLINEARとして読み取ります");
            hasLoggedCubicSpline = true;
        }
        auto valueElement = [isCubicSpline](size_t key) { return isCubicSpline ? key * 3 + 1 : key; };

        std::vector<double> times = ReadAccessorRaw(gltf, (int32_t)sampler["input"].AsNumber(), buffers);
        std::vector<double> values = ReadAccessorRaw(gltf, (int32_t)sampler["output"].AsNumber(), buffers);
        float& animationDuration = animation.duration;

        if (path == "weights") {
            // キーごとにターゲットの数だけ重みが並ぶ（CUBICSPLINE は接線の分で3倍）
            const size_t valuesPerKey = times.empty() ? 0 : values.size() / times.size() / (isCubicSpline ? 3 : 1);
            if (valuesPerKey == 0) continue;
            GltfWeightsAnimation& weightsAnimation = animation.weightsAnimations[targetNodeIndex];
            weightsAnimation.interpolation = interpolation;
            for (size_t k = 0; k < times.size(); ++k) {
                const size_t v = valueElement(k) * valuesPerKey;
                if (v + valuesPerKey > values.size()) break;
                weightsAnimation.times.push_back((float)times[k]);
                std::vector<float> keyWeights(valuesPerKey);
                for (size_t w = 0; w < valuesPerKey; ++w) keyWeights[w] = (float)values[v + w];
                weightsAnimation.weights.push_back(std::move(keyWeights));
                animationDuration = (std::max)(animationDuration, (float)times[k]);
            }
            continue;
        }

        GltfNodeAnimation& nodeAnimation = animation.nodeAnimations[targetNodeIndex];
        if (path == "rotation") {
            nodeAnimation.rotateInterpolation = interpolation;
            for (size_t k = 0; k < times.size(); ++k) {
                const size_t v = valueElement(k) * 4;
                if (v + 3 >= values.size()) break;
                Quaternion value = ConvertRotation({ (float)values[v + 0], (float)values[v + 1], (float)values[v + 2], (float)values[v + 3] });
                nodeAnimation.rotate.push_back({ (float)times[k], value });
                animationDuration = (std::max)(animationDuration, (float)times[k]);
            }
        } else {
            const bool isTranslate = path == "translation";
            std::vector<Vector3Key>& keys = isTranslate ? nodeAnimation.translate : nodeAnimation.scale;
            (isTranslate ? nodeAnimation.translateInterpolation : nodeAnimation.scaleInterpolation) = interpolation;
            for (size_t k = 0; k < times.size(); ++k) {
                const size_t v = valueElement(k) * 3;
                if (v + 2 >= values.size()) break;
                Vector3 value = { (float)values[v + 0], (float)values[v + 1], (float)values[v + 2] };
                keys.push_back({ (float)times[k], isTranslate ? ConvertPosition(value) : value });
                animationDuration = (std::max)(animationDuration, (float)times[k]);
            }
        }
    }
    return animation;
}

// images を読む（外部ファイルはパスだけ、データURI・bufferView の画像は中身のバイト列を取り出す）
std::vector<GltfImage> GltfLoader::ParseImages(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers,
                                               const std::string& directoryPath, const std::string& filePath) {
    std::vector<GltfImage> images;
    if (!gltf.Contains("images")) return images;
    const JsonValue& imagesJson = gltf["images"];
    images.resize(imagesJson.Size());
    for (size_t i = 0; i < imagesJson.Size(); ++i) {
        const JsonValue& imageJson = imagesJson[i];
        GltfImage& image = images[i];
        // 中に入っている画像は、ファイルのパス + 番号を TextureManager の名前にする（他の glTF と重ならない）
        image.name = filePath + "#image" + std::to_string(i);
        if (imageJson.Contains("uri")) {
            const std::string& uri = imageJson["uri"].AsString();
            size_t base64Marker = uri.find(";base64,");
            if (uri.compare(0, 5, "data:") == 0 && base64Marker != std::string::npos) {
                image.data = DecodeBase64(uri.substr(base64Marker + 8));
            } else {
                image.filePath = directoryPath + "/" + DecodeUri(uri);
                image.name = image.filePath;
            }
        } else if (imageJson.Contains("bufferView")) {
            const JsonValue& bufferView = gltf["bufferViews"][(size_t)imageJson["bufferView"].AsNumber()];
            size_t bufferIndex = (size_t)bufferView["buffer"].AsNumber();
            size_t byteOffset = bufferView.Contains("byteOffset") ? (size_t)bufferView["byteOffset"].AsNumber() : 0;
            size_t byteLength = (size_t)bufferView["byteLength"].AsNumber();
            if (bufferIndex >= buffers.size() || byteOffset + byteLength > buffers[bufferIndex].size()) {
                Fail("GltfLoader: 画像のbufferViewがbufferの範囲外です");
            }
            const uint8_t* begin = buffers[bufferIndex].data() + byteOffset;
            image.data.assign(begin, begin + byteLength);
        }
    }
    return images;
}

// materials の基本色（pbrMetallicRoughness.baseColorFactor / baseColorTexture）を読む
std::vector<GltfMaterial> GltfLoader::ParseMaterials(const JsonValue& gltf) {
    std::vector<GltfMaterial> materials;
    if (!gltf.Contains("materials")) return materials;
    const JsonValue& materialsJson = gltf["materials"];
    materials.resize(materialsJson.Size());
    for (size_t i = 0; i < materialsJson.Size(); ++i) {
        const JsonValue& materialJson = materialsJson[i];
        GltfMaterial& material = materials[i];
        if (materialJson.Contains("name")) material.name = materialJson["name"].AsString();
        const JsonValue& pbr = materialJson["pbrMetallicRoughness"];
        if (pbr.Contains("baseColorFactor")) {
            const JsonValue& f = pbr["baseColorFactor"];
            material.baseColorFactor = { (float)f[(size_t)0].AsNumber(), (float)f[(size_t)1].AsNumber(), (float)f[(size_t)2].AsNumber(), (float)f[(size_t)3].AsNumber() };
        }
        if (pbr.Contains("baseColorTexture")) {
            const JsonValue& texture = gltf["textures"][(size_t)pbr["baseColorTexture"]["index"].AsNumber()];
            if (texture.Contains("source")) {
                material.imageIndex = (int32_t)texture["source"].AsNumber();
            }
        }
    }
    return materials;
}

std::string GltfLoader::LoadMaterialTexture(const GltfModelData& data, int32_t materialIndex) {
    if (materialIndex < 0 || (size_t)materialIndex >= data.materials.size()) {
        return "white";
    }
    const GltfMaterial& material = data.materials[(size_t)materialIndex];
    TextureManager* textureManager = TextureManager::GetInstance();
    if (material.imageIndex >= 0 && (size_t)material.imageIndex < data.images.size()) {
        const GltfImage& image = data.images[(size_t)material.imageIndex];
        if (!image.filePath.empty()) {
            textureManager->LoadTexture(image.filePath);
            return image.filePath;
        }
        if (!image.data.empty()) {
            textureManager->LoadTextureFromMemory(image.name, image.data.data(), image.data.size());
            return image.name;
        }
    }

    // 画像の無いマテリアルは基本色の 1x1 の画像で塗る（同じ色は使い回す）
    auto toByte = [](float value) { return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    const Vector4& color = material.baseColorFactor;
    const uint8_t pixel[4] = { toByte(color.x), toByte(color.y), toByte(color.z), toByte(color.w) };
    if (pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255 && pixel[3] == 255) {
        return "white";
    }
    std::string name = std::format("<gltf-color>{:02x}{:02x}{:02x}{:02x}", pixel[0], pixel[1], pixel[2], pixel[3]);
    if (!textureManager->HasTexture(name)) {
        textureManager->CreateOrUpdateTexture(name, 1, 1, pixel);
    }
    return name;
}

Matrix4x4 GltfLoader::ComputeNodeWorldMatrix(const std::vector<GltfNode>& nodes, int32_t nodeIndex) {
    // 行ベクトルなので 自分 * 親 * 親の親 ... の順に掛ける
    Matrix4x4 world = Calculation::MakeIdentity4x4();
    for (int32_t i = nodeIndex; i >= 0 && (size_t)i < nodes.size(); i = nodes[(size_t)i].parent) {
        const GltfNode& node = nodes[(size_t)i];
        world = world * Calculation::MakeAffineMatrix(node.scale, node.rotate, node.translate);
    }
    return world;
}

std::vector<VertexData> GltfLoader::BakeRestPose(const GltfModelData& data, const std::vector<VertexData>& vertices) {
    // 骨ごとの行列 = 逆バインド行列 * 骨のレストポーズのワールド行列（SkinCluster と同じ計算）
    const GltfSkin& skin = data.skin;
    const size_t jointCount = (std::min)(skin.jointNodeIndices.size(), skin.inverseBindMatrices.size());
    std::vector<Matrix4x4> skinMatrices(jointCount);
    for (size_t j = 0; j < jointCount; ++j) {
        skinMatrices[j] = skin.inverseBindMatrices[j] * ComputeNodeWorldMatrix(data.nodes, skin.jointNodeIndices[j]);
    }

    std::vector<VertexData> result = vertices;
    for (VertexData& v : result) {
        const Vector3 position = { v.position.x, v.position.y, v.position.z };
        const float weights[4] = { v.boneWeights.x, v.boneWeights.y, v.boneWeights.z, v.boneWeights.w };
        Vector3 bakedPosition = { 0.0f, 0.0f, 0.0f };
        Vector3 bakedNormal = { 0.0f, 0.0f, 0.0f };
        for (size_t k = 0; k < 4; ++k) {
            if (weights[k] <= 0.0f || v.boneIndices[k] >= jointCount) continue;
            const Matrix4x4& m = skinMatrices[v.boneIndices[k]];
            bakedPosition += Calculation::Transform(position, m) * weights[k];
            bakedNormal += TransformDirection(v.normal, m) * weights[k];
        }
        v.position = { bakedPosition.x, bakedPosition.y, bakedPosition.z, 1.0f };
        v.normal = Calculation::Normalize(bakedNormal);
        v.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
        std::fill(std::begin(v.boneIndices), std::end(v.boneIndices), 0u);
    }
    return result;
}

void GltfLoader::ApplyMorphTargets(const std::vector<VertexData>& baseVertices, const std::vector<GltfMorphTarget>& targets,
                                   const std::vector<float>& weights, VertexData* outVertices) {
    const size_t vertexCount = baseVertices.size();
    for (size_t v = 0; v < vertexCount; ++v) {
        outVertices[v] = baseVertices[v];
    }
    bool hasNormalDelta = false;
    for (size_t t = 0; t < targets.size() && t < weights.size(); ++t) {
        const float weight = weights[t];
        if (weight == 0.0f) continue;
        const GltfMorphTarget& target = targets[t];
        for (size_t v = 0; v < vertexCount && v < target.positionDeltas.size(); ++v) {
            const Vector3& delta = target.positionDeltas[v];
            outVertices[v].position.x += delta.x * weight;
            outVertices[v].position.y += delta.y * weight;
            outVertices[v].position.z += delta.z * weight;
        }
        for (size_t v = 0; v < vertexCount && v < target.normalDeltas.size(); ++v) {
            outVertices[v].normal += target.normalDeltas[v] * weight;
            hasNormalDelta = true;
        }
    }
    if (hasNormalDelta) {
        for (size_t v = 0; v < vertexCount; ++v) {
            outVertices[v].normal = Calculation::Normalize(outVertices[v].normal);
        }
    }
}

GltfModelData GltfLoader::LoadGltfFile(const std::string& directoryPath, const std::string& filename) {
    std::string filePath = directoryPath + "/" + filename;
    std::vector<uint8_t> fileBytes = ReadFileBytes(filePath);

    // .glb は先頭が "glTF"。それ以外はテキストの .gltf として読む
    std::string jsonText;
    std::vector<uint8_t> glbBinChunk;
    if (fileBytes.size() >= sizeof(uint32_t) && ReadUint32(fileBytes, 0) == kGlbMagic) {
        SplitGlb(fileBytes, filePath, jsonText, glbBinChunk);
    } else {
        jsonText.assign(fileBytes.begin(), fileBytes.end());
    }

    JsonValue gltf = JsonParser::Parse(jsonText); // 構文エラー時はJsonParser内でLogger::Log+例外

    std::vector<std::vector<uint8_t>> buffers;
    if (gltf.Contains("buffers")) {
        const JsonValue& buffersJson = gltf["buffers"];
        buffers.resize(buffersJson.Size());
        for (size_t i = 0; i < buffersJson.Size(); ++i) {
            buffers[i] = LoadBufferData(gltf, (int32_t)i, directoryPath, glbBinChunk);
        }
    }

    GltfModelData result;
    // メッシュはノードの位置を焼き込むので、ノードを先に読む
    result.nodes = ParseNodes(gltf);
    ParseMeshes(gltf, buffers, result);
    result.skin = ParseSkin(gltf, buffers);
    result.animations = ParseAnimations(gltf, buffers, result.nodes.size());
    result.images = ParseImages(gltf, buffers, directoryPath, filePath);
    result.materials = ParseMaterials(gltf);
    return result;
}

void GltfLoader::SplitGlb(const std::vector<uint8_t>& fileBytes, const std::string& filePath,
                          std::string& jsonText, std::vector<uint8_t>& binChunk) {
    // ヘッダー12バイト（"glTF"・バージョン・全体の長さ）の後に、チャンク（長さ・種類・中身）が並ぶ
    constexpr size_t kHeaderSize = 12;
    constexpr size_t kChunkHeaderSize = 8;
    if (fileBytes.size() < kHeaderSize || ReadUint32(fileBytes, 4) != 2) {
        Fail("GltfLoader: バージョン2以外の.glbは非対応です: " + filePath);
    }
    size_t offset = kHeaderSize;
    while (offset + kChunkHeaderSize <= fileBytes.size()) {
        const uint32_t chunkLength = ReadUint32(fileBytes, offset);
        const uint32_t chunkType = ReadUint32(fileBytes, offset + 4);
        const size_t dataStart = offset + kChunkHeaderSize;
        if (dataStart + chunkLength > fileBytes.size()) {
            Fail("GltfLoader: .glbのチャンクがファイルの範囲外です: " + filePath);
        }
        const uint8_t* data = fileBytes.data() + dataStart;
        if (chunkType == kGlbChunkJson) {
            jsonText.assign(reinterpret_cast<const char*>(data), chunkLength);
        } else if (chunkType == kGlbChunkBin && binChunk.empty()) {
            binChunk.assign(data, data + chunkLength);
        }
        offset = dataStart + chunkLength;
    }
    if (jsonText.empty()) {
        Fail("GltfLoader: .glbにJSONチャンクがありません: " + filePath);
    }
}

std::vector<uint8_t> GltfLoader::LoadBufferData(const JsonValue& gltf, int32_t bufferIndex, const std::string& directoryPath,
                                                const std::vector<uint8_t>& glbBinChunk) {
    const JsonValue& buffer = gltf["buffers"][(size_t)bufferIndex];
    // uri を持たない buffer は .glb の BIN チャンクを指す
    if (!buffer.Contains("uri")) {
        if (glbBinChunk.empty()) {
            Fail("GltfLoader: uriの無いbufferがありますが、.glbのBINチャンクがありません");
        }
        return glbBinChunk;
    }
    std::string uri = buffer["uri"].AsString();

    // data URI（"data:<mime>;base64,<...>"）はMIMEタイプを問わず";base64,"以降をデコードする
    size_t base64Marker = uri.find(";base64,");
    if (uri.compare(0, 5, "data:") == 0 && base64Marker != std::string::npos) {
        return DecodeBase64(uri.substr(base64Marker + 8));
    }

    // 外部.binファイル参照
    return ReadFileBytes(directoryPath + "/" + DecodeUri(uri));
}

std::vector<double> GltfLoader::ReadAccessorRaw(const JsonValue& gltf, int32_t accessorIndex, const std::vector<std::vector<uint8_t>>& buffers) {
    const JsonValue& accessor = gltf["accessors"][(size_t)accessorIndex];
    int32_t componentType = (int32_t)accessor["componentType"].AsNumber();
    std::string type = accessor["type"].AsString();
    size_t count = (size_t)accessor["count"].AsNumber();
    size_t componentCount = TypeComponentCount(type);
    size_t componentSize = ComponentByteSize(componentType);
    size_t tightStride = componentCount * componentSize;
    const bool isNormalized = accessor.Contains("normalized") && accessor["normalized"].AsBool();

    // bufferView[index] が指す buffer と、その中での始まりの位置
    auto resolveBufferView = [&](size_t bufferViewIndex, size_t& viewOffset, size_t& viewStride) -> const std::vector<uint8_t>& {
        const JsonValue& bufferView = gltf["bufferViews"][bufferViewIndex];
        int32_t bufferIndex = (int32_t)bufferView["buffer"].AsNumber();
        if (bufferIndex < 0 || (size_t)bufferIndex >= buffers.size()) {
            Fail("GltfLoader: bufferViewが参照するbufferのインデックスが不正です");
        }
        viewOffset = bufferView.Contains("byteOffset") ? (size_t)bufferView["byteOffset"].AsNumber() : 0;
        viewStride = bufferView.Contains("byteStride") ? (size_t)bufferView["byteStride"].AsNumber() : 0;
        return buffers[(size_t)bufferIndex];
    };
    auto readValue = [&](const std::vector<uint8_t>& buffer, size_t byteOffset, int32_t valueType) {
        if (byteOffset + ComponentByteSize(valueType) > buffer.size()) {
            Fail("GltfLoader: accessorが読み取るバイト範囲がbufferの範囲外です");
        }
        return ReadComponentValue(buffer, byteOffset, valueType);
    };

    // bufferView が無い accessor は全部 0（sparse で一部だけ値が入ることがある）
    std::vector<double> result(count * componentCount, 0.0);
    if (accessor.Contains("bufferView")) {
        size_t viewOffset = 0;
        size_t byteStride = 0;
        const std::vector<uint8_t>& buffer = resolveBufferView((size_t)accessor["bufferView"].AsNumber(), viewOffset, byteStride);
        if (byteStride == 0) byteStride = tightStride;
        const size_t baseOffset = viewOffset + (accessor.Contains("byteOffset") ? (size_t)accessor["byteOffset"].AsNumber() : 0);
        for (size_t i = 0; i < count; ++i) {
            for (size_t c = 0; c < componentCount; ++c) {
                result[i * componentCount + c] = readValue(buffer, baseOffset + i * byteStride + c * componentSize, componentType);
            }
        }
    }

    // sparse: 「何番目の要素を」「どの値に」差し替えるかの一覧
    const JsonValue& sparse = accessor["sparse"];
    if (sparse.IsObject()) {
        const size_t sparseCount = (size_t)sparse["count"].AsNumber();
        const JsonValue& indicesJson = sparse["indices"];
        const JsonValue& valuesJson = sparse["values"];
        const int32_t indexType = (int32_t)indicesJson["componentType"].AsNumber();
        size_t indexOffset = 0;
        size_t valueOffset = 0;
        size_t unusedStride = 0;
        const std::vector<uint8_t>& indexBuffer = resolveBufferView((size_t)indicesJson["bufferView"].AsNumber(), indexOffset, unusedStride);
        const std::vector<uint8_t>& valueBuffer = resolveBufferView((size_t)valuesJson["bufferView"].AsNumber(), valueOffset, unusedStride);
        indexOffset += indicesJson.Contains("byteOffset") ? (size_t)indicesJson["byteOffset"].AsNumber() : 0;
        valueOffset += valuesJson.Contains("byteOffset") ? (size_t)valuesJson["byteOffset"].AsNumber() : 0;
        for (size_t i = 0; i < sparseCount; ++i) {
            const size_t element = (size_t)readValue(indexBuffer, indexOffset + i * ComponentByteSize(indexType), indexType);
            if (element >= count) {
                Fail("GltfLoader: sparseの番号が要素数を超えています");
            }
            for (size_t c = 0; c < componentCount; ++c) {
                result[element * componentCount + c] = readValue(valueBuffer, valueOffset + (i * componentCount + c) * componentSize, componentType);
            }
        }
    }

    if (isNormalized) {
        for (double& value : result) {
            value = NormalizeComponent(value, componentType);
        }
    }
    return result;
}

Matrix4x4 GltfLoader::ConvertGltfMatrix(const std::array<float, 16>& m) {
    // glTFは列優先ストレージ・列ベクトル規約: 生配列は m[col*4+row] が数学上の要素(row,col)。
    // 本エンジンは行優先ストレージ・行ベクトル規約(m.m[row][col]が数学上の要素(row,col))で、
    // 変換には数学的な転置（M_engine = M_gltf^T、つまりM_engine(row,col) = M_gltf(col,row)）が必要。
    // 列優先の生配列を「行優先として素直に読む」だけで自動的にこの転置になる（m[row*4+col]）。
    // ※誤って m[col*4+row] にすると転置されず元の行列のままになり、平行移動成分が
    //   m[0][3]/[1][3]/[2][3]（本来使われない列）に迷い込んで消えてしまう
    Matrix4x4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result.m[row][col] = m[(size_t)(row * 4 + col)];
        }
    }
    return result;
}

size_t GltfLoader::ComponentByteSize(int32_t componentType) {
    switch (componentType) {
        case 5120: return 1; // BYTE
        case 5121: return 1; // UNSIGNED_BYTE
        case 5122: return 2; // SHORT
        case 5123: return 2; // UNSIGNED_SHORT
        case 5125: return 4; // UNSIGNED_INT
        case 5126: return 4; // FLOAT
        default:
            Logger::Log("GltfLoader: 未対応のcomponentTypeです: " + std::to_string(componentType));
            return 4;
    }
}

size_t GltfLoader::TypeComponentCount(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    Logger::Log("GltfLoader: 未対応のaccessor typeです: " + type);
    return 1;
}

double GltfLoader::ReadComponentValue(const std::vector<uint8_t>& buffer, size_t byteOffset, int32_t componentType) {
    switch (componentType) {
        case 5120: { int8_t v; std::memcpy(&v, &buffer[byteOffset], sizeof(v)); return (double)v; }
        case 5121: { uint8_t v; std::memcpy(&v, &buffer[byteOffset], sizeof(v)); return (double)v; }
        case 5122: { int16_t v; std::memcpy(&v, &buffer[byteOffset], sizeof(v)); return (double)v; }
        case 5123: { uint16_t v; std::memcpy(&v, &buffer[byteOffset], sizeof(v)); return (double)v; }
        case 5125: { uint32_t v; std::memcpy(&v, &buffer[byteOffset], sizeof(v)); return (double)v; }
        case 5126: { float v; std::memcpy(&v, &buffer[byteOffset], sizeof(v)); return (double)v; }
        default:
            Logger::Log("GltfLoader: 未対応のcomponentTypeです: " + std::to_string(componentType));
            return 0.0;
    }
}

std::vector<uint8_t> GltfLoader::DecodeBase64(const std::string& base64) {
    auto decodeChar = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1; // '=' 等のパディング・無効文字は無視
    };

    std::vector<uint8_t> result;
    result.reserve(base64.size() / 4 * 3);
    int buffer = 0;
    int bitsCollected = 0;
    for (char c : base64) {
        int value = decodeChar(c);
        if (value < 0) continue;
        buffer = (buffer << 6) | value;
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            result.push_back(static_cast<uint8_t>((buffer >> bitsCollected) & 0xFF));
        }
    }
    return result;
}
