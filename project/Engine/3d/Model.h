#pragma once
#include"Structs.h"
#include"Calculation.h"
#include<string>
#include<vector>
#include"DirectXCommon.h"
#include"TextureManager.h"
#include"GltfTypes.h"

class Model{
public:
public:
    // ロードとリソース作成を一度に行う関数
    void Initialize(DirectXCommon* dxCommon, const std::string& directoryPath, const std::string& filename);
    // OBJファイルを介さず、頂点データを直接渡して初期化する（プロシージャル生成用。外部リソース不要）
    void InitializeFromVertices(DirectXCommon* dxCommon, const std::vector<VertexData>& vertices, const std::string& textureFilePath = "white");
    // glTF の全プリミティブから作る（プリミティブごとに別の画像で描く）。
    // keepSkinning: true なら骨の重みを残し、骨で動かないメッシュもノードのアニメーションで動くようにする
    //               （SkinnedObject3d 用。SkinnedObject3d::SetGltfData が中で呼ぶ）。
    //               false なら骨で動くメッシュをレストポーズの形に直す（Object3d・ModelObject で動かさずに表示する用）
    void InitializeFromGltf(DirectXCommon* dxCommon, const GltfModelData& data, bool keepSkinning);
    // instanceCount: 同じモデルを何個まとめて描くか（インスタンシング用。通常は1）
    void Draw(ID3D12GraphicsCommandList* commandList, UINT instanceCount = 1);

    // モーフターゲットを持つ部分があるか（InitializeFromGltf を keepSkinning = true で作ったときだけ）
    bool HasMorphTargets() const { return !morphParts.empty(); }
    // nodeIndex のノードに置かれた部分のモーフの重みを変えて、頂点を作り直す（SkinnedObject3d が Update で呼ぶ）。
    // 前のフレームの描画は終わっている前提で、頂点バッファを直接書き換える
    void ApplyMorphWeights(int32_t nodeIndex, const std::vector<float>& weights);

    // Getter
    const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView; }
    // 全部の部分を合わせた頂点数（影のように画像を使わない描画は、これで一度に描ける）
    UINT GetVertexCount() const { return (UINT)modelData.vertices.size(); }

private:
    // 1枚の画像で描く部分（OBJ は1つ、glTF はプリミティブの数だけ）
    struct SubMesh {
        uint32_t startVertex = 0;
        uint32_t vertexCount = 0;
        // 使っているテクスチャ（モデルが消えると使用数が減る）
        TextureHandle texture;
    };
    // モーフする部分（glTF のプリミティブ1つ分）
    struct MorphPart {
        uint32_t startVertex = 0;
        // この部分を置いているノード（重みはノードごと）
        int32_t nodeIndex = -1;
        // 重み 0 のときの頂点
        std::vector<VertexData> baseVertices;
        std::vector<GltfMorphTarget> targets;
    };

private:
    // 既存のメンバ関数はprivateにして、Initializeから呼ぶのが一般的です
    MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
    ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
    // 頂点リソース作成〜転送までの共通処理(Initialize/InitializeFromVerticesの両方から呼ばれる)
    void CreateVertexBuffer(DirectXCommon* dxCommon);
    // 頂点全体を1枚の画像で描く部分を1つだけ作る
    void SetSingleSubMesh(const std::string& textureFilePath);

    ModelData modelData;
    std::vector<SubMesh> subMeshes;
    std::vector<MorphPart> morphParts;
    // モーフするときだけ、頂点バッファを Map したままにしておく（書き換え用）
    VertexData* mappedVertices = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

