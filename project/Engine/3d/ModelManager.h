#pragma once
#include "Model.h"
#include "GltfTypes.h"
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <memory>

class ModelManager {
public:
	static ModelManager* GetInstance();

	// 初期化
	void Initialize(DirectXCommon* dxCommon);

	// 終了
	void Finalize();

	// モデルのロード（すでに読み込み済みならそれを返す）
	Model* LoadModel(const std::string& directoryPath, const std::string& filename);

	// 1辺1・中心が原点の白い立方体（ファイル不要。初回に作って使い回す）。色は Object3d::SetColor で付ける
	Model* GetCubeModel();
	// 半径0.5・中心が原点の白い球（GetCubeModel と同じく使い回す）
	Model* GetSphereModel();

	// glTFモデルのロード（すでに読み込み済みならそれを返す）。SkinnedObject3d::SetGltfData()に渡して使う。
	GltfModelData* LoadGltfModel(const std::string& directoryPath, const std::string& filename);

private:
	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;

	// 頂点から作るモデルを key でキャッシュする（初回だけ buildVertices を呼ぶ）
	Model* GetPrimitiveModel(const std::string& key, const std::function<std::vector<VertexData>()>& buildVertices);

	DirectXCommon* dxCommon = nullptr;

	// モデルのキャッシュ
	std::map<std::string, std::unique_ptr<Model>> models;
	// glTFモデルのキャッシュ
	std::map<std::string, std::unique_ptr<GltfModelData>> gltfModels;
};
