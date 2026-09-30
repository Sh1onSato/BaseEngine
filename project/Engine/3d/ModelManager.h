#pragma once
#include "Model.h"
#include "GltfTypes.h"
#include <map>
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

	// glTFモデルのロード（すでに読み込み済みならそれを返す）。SkinnedObject3d::SetGltfData()に渡して使う。
	GltfModelData* LoadGltfModel(const std::string& directoryPath, const std::string& filename);

private:
	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;

	DirectXCommon* dxCommon = nullptr;

	// モデルのキャッシュ
	std::map<std::string, std::unique_ptr<Model>> models;
	// glTFモデルのキャッシュ
	std::map<std::string, std::unique_ptr<GltfModelData>> gltfModels;
};
