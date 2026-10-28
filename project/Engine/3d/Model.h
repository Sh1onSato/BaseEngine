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

    // ===== 材質（このモデルを使う全部の物に効く。ModelManager のモデルは共有なので注意。物ごとに変えるなら Object3d::SetMetallic など） =====
    // 部分（glTF のプリミティブ。OBJ は1つ）の数。part に -1 を渡すと全部の部分
    size_t GetPartCount() const { return subMeshes.size(); }
    // 法線マップ（細かい凹凸を光の当たり方だけで見せる画像。glTF・Blender と同じ向き）。strength で凹凸の強さ
    void SetNormalMap(const std::string& filePath, float strength = 1.0f, int part = -1);
    // 金属・粗さの画像（glTF と同じ: 緑 = 粗さ、青 = 金属。SetMetallic・SetRoughness の値に掛かる）
    void SetMetallicRoughnessMap(const std::string& filePath, int part = -1);
    // 発光の画像（SetEmissive の色に掛かる）・影になりやすい所の画像（赤を使う）
    void SetEmissiveMap(const std::string& filePath, int part = -1);
    void SetOcclusionMap(const std::string& filePath, float strength = 1.0f, int part = -1);
    // 金属らしさ（0〜1）と粗さ（0 = つるつる 〜 1 = ざらざら）。呼ぶと金属・粗さを使う当たり方になる
    void SetMetallic(float metallic, int part = -1);
    void SetRoughness(float roughness, int part = -1);
    // 自分で光る色（intensity を掛ける。1 を超えると画面の効果のブルームで光って見える）
    void SetEmissive(const Vector3& color, float intensity = 1.0f, int part = -1);
    // true: 金属・粗さを使う当たり方（照り返しがある）、false: 今までの簡単な当たり方。glTF のモデルは最初から true
    void SetPhysicalLighting(bool enable, int part = -1);
    // 部分の材質を直接読み書きする（範囲外なら最初の部分）
    SurfaceMaterial& GetSurfaceMaterial(size_t part);

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
        // 材質（金属・粗さ・発光など）と、その画像（使わない物は白。並びは Object3dCommon::kRootIndexMaterialTextures の順）
        Microsoft::WRL::ComPtr<ID3D12Resource> surfaceResource;
        SurfaceMaterial* surface = nullptr;
        TextureHandle materialTextures[4];
    };
    // 材質の画像の並び（SubMesh::materialTextures の添字）
    enum MaterialTextureSlot { kNormalSlot, kMetallicRoughnessSlot, kEmissiveSlot, kOcclusionSlot };
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
    // 部分の材質の定数バッファを作り、画像を全部白にする
    void InitializeSurface(SubMesh& subMesh);
    // .mtl の追加の項目を部分の材質に入れる
    void ApplyMaterialData(const MaterialData& material);
    // glTF のマテリアル（金属・粗さ・画像）を部分の材質に入れる
    void ApplyGltfMaterial(SubMesh& subMesh, const GltfModelData& data, int32_t materialIndex);
    // part（-1 なら全部）の部分ごとに function を呼ぶ
    template <typename Function>
    void ForEachPart(int part, Function function);
    // 部分の画像の1枚を差し替え、使う印を flags に立てる
    void SetMaterialTexture(int part, MaterialTextureSlot slot, const std::string& textureName, uint32_t flag);

    DirectXCommon* dxCommon = nullptr;

    ModelData modelData;
    std::vector<SubMesh> subMeshes;
    std::vector<MorphPart> morphParts;
    // モーフするときだけ、頂点バッファを Map したままにしておく（書き換え用）
    VertexData* mappedVertices = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

