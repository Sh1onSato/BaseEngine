#include "ModelManager.h"
#include "GltfLoader.h"
#include "PrimitiveMesh.h"

ModelManager* ModelManager::GetInstance() {
	static ModelManager instance;
	return &instance;
}

void ModelManager::Initialize(DirectXCommon* dxCommon) {
	this->dxCommon = dxCommon;
}

void ModelManager::Finalize() {
	models.clear();
	gltfModels.clear();
}

Model* ModelManager::LoadModel(const std::string& directoryPath, const std::string& filename) {
	// すでにロード済みかチェック
	std::string filePath = directoryPath + "/" + filename;
	if (models.contains(filePath)) {
		return models.at(filePath).get();
	}

	// 新しく生成して初期化
	std::unique_ptr<Model> model = std::make_unique<Model>();
	model->Initialize(dxCommon, directoryPath, filename);

	// キャッシュに保存して返す
	models[filePath] = std::move(model);
	return models.at(filePath).get();
}

Model* ModelManager::GetCubeModel() {
	return GetPrimitiveModel("<primitive>/cube", PrimitiveMesh::BuildCube);
}

Model* ModelManager::GetSphereModel() {
	return GetPrimitiveModel("<primitive>/sphere", [] { return PrimitiveMesh::BuildSphere(16, 12); });
}

Model* ModelManager::GetPrimitiveModel(const std::string& key, const std::function<std::vector<VertexData>()>& buildVertices) {
	// ファイルパスとは重ならない名前でキャッシュに入れる（Finalize で他のモデルと一緒に消える）
	if (models.contains(key)) {
		return models.at(key).get();
	}
	std::unique_ptr<Model> model = std::make_unique<Model>();
	model->InitializeFromVertices(dxCommon, buildVertices(), "white");
	models[key] = std::move(model);
	return models.at(key).get();
}

GltfModelData* ModelManager::LoadGltfModel(const std::string& directoryPath, const std::string& filename) {
	// すでにロード済みかチェック
	std::string filePath = directoryPath + "/" + filename;
	if (gltfModels.contains(filePath)) {
		return gltfModels.at(filePath).get();
	}

	// 新しく読み込んでキャッシュに保存
	std::unique_ptr<GltfModelData> gltfModel = std::make_unique<GltfModelData>(GltfLoader::LoadGltfFile(directoryPath, filename));
	gltfModels[filePath] = std::move(gltfModel);
	return gltfModels.at(filePath).get();
}
