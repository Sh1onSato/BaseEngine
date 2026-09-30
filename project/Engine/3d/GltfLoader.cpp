#include "GltfLoader.h"
#include "JsonParser.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>

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

// scenes[scene].nodes[0]（先頭のルートノード）のインデックスを返す。見つからなければ-1。
int32_t GltfLoader::FindRootNodeIndex(const JsonValue& gltf) {
    if (!gltf.Contains("scenes")) return -1;
    const JsonValue& scenesJson = gltf["scenes"];
    size_t sceneIndex = gltf.Contains("scene") ? (size_t)gltf["scene"].AsNumber() : 0;
    if (sceneIndex >= scenesJson.Size()) return -1;
    const JsonValue& sceneJson = scenesJson[sceneIndex];
    if (!sceneJson.Contains("nodes") || sceneJson["nodes"].Size() == 0) return -1;
    return (int32_t)sceneJson["nodes"][(size_t)0].AsNumber();
}

// meshes[0].primitives[0] からVertexDataの配列を組み立てる（indices指定があれば展開して非インデックス化する）
std::vector<VertexData> GltfLoader::ParseVertices(const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers) {
    std::vector<VertexData> vertices;
    if (!gltf.Contains("meshes") || gltf["meshes"].Size() == 0) return vertices;
    const JsonValue& mesh = gltf["meshes"][(size_t)0];
    if (!mesh.Contains("primitives") || mesh["primitives"].Size() == 0) return vertices;
    const JsonValue& primitive = mesh["primitives"][(size_t)0];
    const JsonValue& attributes = primitive["attributes"];

    std::vector<double> positions = attributes.Contains("POSITION") ? ReadAccessorRaw(gltf, (int32_t)attributes["POSITION"].AsNumber(), buffers) : std::vector<double>();
    std::vector<double> normals = attributes.Contains("NORMAL") ? ReadAccessorRaw(gltf, (int32_t)attributes["NORMAL"].AsNumber(), buffers) : std::vector<double>();
    std::vector<double> texcoords = attributes.Contains("TEXCOORD_0") ? ReadAccessorRaw(gltf, (int32_t)attributes["TEXCOORD_0"].AsNumber(), buffers) : std::vector<double>();
    std::vector<double> joints = attributes.Contains("JOINTS_0") ? ReadAccessorRaw(gltf, (int32_t)attributes["JOINTS_0"].AsNumber(), buffers) : std::vector<double>();
    std::vector<double> weights = attributes.Contains("WEIGHTS_0") ? ReadAccessorRaw(gltf, (int32_t)attributes["WEIGHTS_0"].AsNumber(), buffers) : std::vector<double>();
    size_t vertexCount = positions.size() / 3;

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

    vertices.reserve(indices.size());
    for (uint32_t idx : indices) {
        VertexData v{};
        v.position = { (float)positions[idx * 3 + 0], (float)positions[idx * 3 + 1], (float)positions[idx * 3 + 2], 1.0f };
        v.normal = normals.empty()
            ? Vector3{ 0.0f, 1.0f, 0.0f }
            : Vector3{ (float)normals[idx * 3 + 0], (float)normals[idx * 3 + 1], (float)normals[idx * 3 + 2] };
        // glTFのTEXCOORD_0は原点が左上でDirectXと同じ規約のため、.objローダーと違い反転不要
        v.texcoord = texcoords.empty()
            ? Vector2{ 0.0f, 0.0f }
            : Vector2{ (float)texcoords[idx * 2 + 0], (float)texcoords[idx * 2 + 1] };
        if (!weights.empty()) {
            v.boneWeights = { (float)weights[idx * 4 + 0], (float)weights[idx * 4 + 1], (float)weights[idx * 4 + 2], (float)weights[idx * 4 + 3] };
        }
        if (!joints.empty()) {
            v.boneIndices[0] = (uint32_t)joints[idx * 4 + 0];
            v.boneIndices[1] = (uint32_t)joints[idx * 4 + 1];
            v.boneIndices[2] = (uint32_t)joints[idx * 4 + 2];
            v.boneIndices[3] = (uint32_t)joints[idx * 4 + 3];
        }
        vertices.push_back(v);
    }
    return vertices;
}

// skins[0] からjointNodeIndicesとinverseBindMatrices(転置済み)を読み取る
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
            skin.inverseBindMatrices.push_back(ConvertGltfMatrix(m));
        }
    } else {
        // glTF仕様上、未指定時は単位行列がデフォルト
        Matrix4x4 identity{};
        for (int i = 0; i < 4; ++i) identity.m[i][i] = 1.0f;
        skin.inverseBindMatrices.assign(skin.jointNodeIndices.size(), identity);
    }
    return skin;
}

// animations[0] からノード名キーのアニメーションチャンネル一式を読み取る
std::map<std::string, GltfNodeAnimation> GltfLoader::ParseAnimations(
    const JsonValue& gltf, const std::vector<std::vector<uint8_t>>& buffers,
    const std::vector<GltfNode>& nodes, float& animationDuration) {

    std::map<std::string, GltfNodeAnimation> nodeAnimations;
    animationDuration = 0.0f;
    if (!gltf.Contains("animations") || gltf["animations"].Size() == 0) return nodeAnimations;

    const JsonValue& animationJson = gltf["animations"][(size_t)0];
    const JsonValue& samplersJson = animationJson["samplers"];
    const JsonValue& channelsJson = animationJson["channels"];

    for (size_t i = 0; i < channelsJson.Size(); ++i) {
        const JsonValue& channel = channelsJson[i];
        const JsonValue& target = channel["target"];
        if (!target.Contains("node")) continue; // ノード対象外のチャンネルは非対応のためスキップ

        int32_t targetNodeIndex = (int32_t)target["node"].AsNumber();
        if (targetNodeIndex < 0 || (size_t)targetNodeIndex >= nodes.size()) continue;
        std::string path = target["path"].AsString();
        if (path != "translation" && path != "rotation" && path != "scale") {
            Logger::Log("GltfLoader: 未対応のアニメーションチャンネルpathをスキップしました: " + path);
            continue;
        }

        int32_t samplerIndex = (int32_t)channel["sampler"].AsNumber();
        const JsonValue& sampler = samplersJson[(size_t)samplerIndex];
        std::string interpolation = sampler.Contains("interpolation") ? sampler["interpolation"].AsString() : "LINEAR";
        if (interpolation != "LINEAR") {
            Logger::Log("GltfLoader: LINEAR以外の補間(" + interpolation + ")は非対応のためLINEARとして読み取ります");
        }

        std::vector<double> times = ReadAccessorRaw(gltf, (int32_t)sampler["input"].AsNumber(), buffers);
        std::vector<double> values = ReadAccessorRaw(gltf, (int32_t)sampler["output"].AsNumber(), buffers);
        std::string nodeName = nodes[(size_t)targetNodeIndex].name;
        GltfNodeAnimation& nodeAnimation = nodeAnimations[nodeName];

        if (path == "rotation") {
            for (size_t k = 0; k < times.size(); ++k) {
                QuaternionKey key{ (float)times[k], { (float)values[k * 4 + 0], (float)values[k * 4 + 1], (float)values[k * 4 + 2], (float)values[k * 4 + 3] } };
                nodeAnimation.rotate.push_back(key);
                animationDuration = (std::max)(animationDuration, key.time);
            }
        } else {
            std::vector<Vector3Key>& keys = (path == "translation") ? nodeAnimation.translate : nodeAnimation.scale;
            for (size_t k = 0; k < times.size(); ++k) {
                Vector3Key key{ (float)times[k], { (float)values[k * 3 + 0], (float)values[k * 3 + 1], (float)values[k * 3 + 2] } };
                keys.push_back(key);
                animationDuration = (std::max)(animationDuration, key.time);
            }
        }
    }
    return nodeAnimations;
}

// images[0].uri から外部テクスチャファイルのパスを組み立てる（data URI画像・テクスチャ無しは非対応/空文字を返す）
std::string GltfLoader::ParseTextureFilePath(const JsonValue& gltf, const std::string& directoryPath) {
    if (!gltf.Contains("images") || gltf["images"].Size() == 0) return "";
    const JsonValue& image = gltf["images"][(size_t)0];
    if (!image.Contains("uri")) return "";
    std::string uri = image["uri"].AsString();
    if (uri.compare(0, 5, "data:") == 0) return ""; // data URI画像はテスト用アセットの範囲外のため非対応
    return directoryPath + "/" + uri;
}

GltfModelData GltfLoader::LoadGltfFile(const std::string& directoryPath, const std::string& filename) {
    std::string filePath = directoryPath + "/" + filename;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::string message = "GltfLoader: ファイルを開けません: " + filePath;
        Logger::Log(message);
        throw std::runtime_error(message);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();

    JsonValue gltf = JsonParser::Parse(ss.str()); // 構文エラー時はJsonParser内でLogger::Log+例外

    std::vector<std::vector<uint8_t>> buffers;
    if (gltf.Contains("buffers")) {
        const JsonValue& buffersJson = gltf["buffers"];
        buffers.resize(buffersJson.Size());
        for (size_t i = 0; i < buffersJson.Size(); ++i) {
            buffers[i] = LoadBufferData(gltf, (int32_t)i, directoryPath);
        }
    }

    GltfModelData result;
    result.vertices = ParseVertices(gltf, buffers);
    result.nodes = ParseNodes(gltf);
    result.rootNodeIndex = FindRootNodeIndex(gltf);
    result.skin = ParseSkin(gltf, buffers);
    result.nodeAnimations = ParseAnimations(gltf, buffers, result.nodes, result.animationDuration);
    result.textureFilePath = ParseTextureFilePath(gltf, directoryPath);
    return result;
}

std::vector<uint8_t> GltfLoader::LoadBufferData(const JsonValue& gltf, int32_t bufferIndex, const std::string& directoryPath) {
    const JsonValue& buffer = gltf["buffers"][(size_t)bufferIndex];
    std::string uri = buffer["uri"].AsString();

    // data URI（"data:<mime>;base64,<...>"）はMIMEタイプを問わず";base64,"以降をデコードする
    size_t base64Marker = uri.find(";base64,");
    if (uri.compare(0, 5, "data:") == 0 && base64Marker != std::string::npos) {
        return DecodeBase64(uri.substr(base64Marker + 8));
    }

    // 外部.binファイル参照
    std::string filePath = directoryPath + "/" + uri;
    std::ifstream binFile(filePath, std::ios::binary | std::ios::ate);
    if (!binFile.is_open()) {
        std::string message = "GltfLoader: バッファファイルを開けません: " + filePath;
        Logger::Log(message);
        throw std::runtime_error(message);
    }
    std::streamsize size = binFile.tellg();
    binFile.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0 && !binFile.read(reinterpret_cast<char*>(data.data()), size)) {
        std::string message = "GltfLoader: バッファファイルの読み込みに失敗しました: " + filePath;
        Logger::Log(message);
        throw std::runtime_error(message);
    }
    return data;
}

std::vector<double> GltfLoader::ReadAccessorRaw(const JsonValue& gltf, int32_t accessorIndex, const std::vector<std::vector<uint8_t>>& buffers) {
    const JsonValue& accessor = gltf["accessors"][(size_t)accessorIndex];
    int32_t componentType = (int32_t)accessor["componentType"].AsNumber();
    std::string type = accessor["type"].AsString();
    size_t count = (size_t)accessor["count"].AsNumber();
    size_t componentCount = TypeComponentCount(type);
    size_t componentSize = ComponentByteSize(componentType);
    size_t tightStride = componentCount * componentSize;

    if (!accessor.Contains("bufferView")) {
        // sparseアクセサ等、bufferViewを持たないケースは非対応
        Logger::Log("GltfLoader: bufferViewを持たないaccessorは非対応です（sparse等）");
        return std::vector<double>(count * componentCount, 0.0);
    }
    int32_t bufferViewIndex = (int32_t)accessor["bufferView"].AsNumber();
    size_t accessorByteOffset = accessor.Contains("byteOffset") ? (size_t)accessor["byteOffset"].AsNumber() : 0;

    const JsonValue& bufferView = gltf["bufferViews"][(size_t)bufferViewIndex];
    int32_t bufferIndex = (int32_t)bufferView["buffer"].AsNumber();
    size_t bufferViewByteOffset = bufferView.Contains("byteOffset") ? (size_t)bufferView["byteOffset"].AsNumber() : 0;
    size_t byteStride = bufferView.Contains("byteStride") ? (size_t)bufferView["byteStride"].AsNumber() : tightStride;

    if (bufferIndex < 0 || (size_t)bufferIndex >= buffers.size()) {
        std::string message = "GltfLoader: bufferViewが参照するbufferのインデックスが不正です";
        Logger::Log(message);
        throw std::runtime_error(message);
    }
    const std::vector<uint8_t>& buffer = buffers[(size_t)bufferIndex];
    size_t baseOffset = bufferViewByteOffset + accessorByteOffset;

    std::vector<double> result;
    result.reserve(count * componentCount);
    for (size_t i = 0; i < count; ++i) {
        size_t elementOffset = baseOffset + i * byteStride;
        for (size_t c = 0; c < componentCount; ++c) {
            size_t byteOffset = elementOffset + c * componentSize;
            if (byteOffset + componentSize > buffer.size()) {
                std::string message = "GltfLoader: accessorが読み取るバイト範囲がbufferの範囲外です";
                Logger::Log(message);
                throw std::runtime_error(message);
            }
            result.push_back(ReadComponentValue(buffer, byteOffset, componentType));
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
