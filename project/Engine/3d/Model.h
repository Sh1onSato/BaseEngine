#pragma once
#include"Structs.h"
#include"Calculation.h"
#include<string>
#include"DirectXCommon.h"
#include"TextureManager.h"

class Model{
public:
public:
    // ロードとリソース作成を一度に行う関数
    void Initialize(DirectXCommon* dxCommon, const std::string& directoryPath, const std::string& filename);
    // OBJファイルを介さず、頂点データを直接渡して初期化する（プロシージャル生成用。外部リソース不要）
    void InitializeFromVertices(DirectXCommon* dxCommon, const std::vector<VertexData>& vertices, const std::string& textureFilePath = "white");
    void Draw(ID3D12GraphicsCommandList* commandList);

    // Getter
    const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView; }
    UINT GetVertexCount() const { return (UINT)modelData.vertices.size(); }

private:

private:
    // 既存のメンバ関数はprivateにして、Initializeから呼ぶのが一般的です
    MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
    ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
    // 頂点リソース作成〜転送までの共通処理(Initialize/InitializeFromVerticesの両方から呼ばれる)
    void CreateVertexBuffer(DirectXCommon* dxCommon);

    ModelData modelData;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};

