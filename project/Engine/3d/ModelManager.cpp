#include "ModelManager.h"
#include "GltfLoader.h"
#include "PrimitiveMesh.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

bool ModelManager::IsGltfFile(const std::string& filename) {
	std::string extension = std::filesystem::path(filename).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension == ".gltf" || extension == ".glb";
}

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

std::shared_ptr<Model> ModelManager::LoadModel(const std::string& directoryPath, const std::string& filename) {
	// すでにロード済みかチェック
	std::string filePath = directoryPath + "/" + filename;
	auto it = models.find(filePath);
	if (it != models.end()) {
		// 新しいシーンでも使われたので、シーン切り替えで解放されないようにする
		it->second.sceneSerial = currentSceneSerial;
		return it->second.data;
	}

	// 新しく生成して初期化
	std::shared_ptr<Model> model = std::make_shared<Model>();
	if (IsGltfFile(filename)) {
		// glTF は中身（GltfModelData）もキャッシュする。骨で動くメッシュはレストポーズの形で止めて表示する
		std::shared_ptr<GltfModelData> gltfData = LoadGltfModel(directoryPath, filename);
		model->InitializeFromGltf(dxCommon, *gltfData, false);
	} else {
		model->Initialize(dxCommon, directoryPath, filename);
	}

	// キャッシュに保存して返す
	models[filePath] = { model, currentSceneSerial, false };
	return model;
}

std::shared_ptr<Model> ModelManager::GetCubeModel() {
	return GetPrimitiveModel("<primitive>/cube", PrimitiveMesh::BuildCube);
}

std::shared_ptr<Model> ModelManager::GetSphereModel() {
	return GetPrimitiveModel("<primitive>/sphere", [] { return PrimitiveMesh::BuildSphere(16, 12); });
}

std::shared_ptr<Model> ModelManager::GetPrimitiveModel(const std::string& key, const std::function<std::vector<VertexData>()>& buildVertices) {
	// ファイルパスとは重ならない名前でキャッシュに入れる（解放はせず、Finalize で他のモデルと一緒に消える）
	auto it = models.find(key);
	if (it != models.end()) {
		return it->second.data;
	}
	std::shared_ptr<Model> model = std::make_shared<Model>();
	model->InitializeFromVertices(dxCommon, buildVertices(), "white");
	models[key] = { model, currentSceneSerial, true };
	return model;
}

std::shared_ptr<GltfModelData> ModelManager::LoadGltfModel(const std::string& directoryPath, const std::string& filename) {
	// すでにロード済みかチェック
	std::string filePath = directoryPath + "/" + filename;
	auto it = gltfModels.find(filePath);
	if (it != gltfModels.end()) {
		it->second.sceneSerial = currentSceneSerial;
		return it->second.data;
	}

	// 新しく読み込んでキャッシュに保存
	std::shared_ptr<GltfModelData> gltfModel = std::make_shared<GltfModelData>(GltfLoader::LoadGltfFile(directoryPath, filename));
	gltfModels[filePath] = { gltfModel, currentSceneSerial, false };
	return gltfModel;
}

void ModelManager::ReleaseUnused() {
	isReleaseAllRequested = true;
}

void ModelManager::OnSceneChanged() {
	++currentSceneSerial;
	releaseBeforeSerial = currentSceneSerial;
}

void ModelManager::Update() {
	ReleaseFrom(models);
	ReleaseFrom(gltfModels);
	isReleaseAllRequested = false;
	releaseBeforeSerial = 0;
}

template <typename T>
void ModelManager::ReleaseFrom(std::map<std::string, CacheEntry<T>>& cache) {
	for (auto it = cache.begin(); it != cache.end();) {
		const CacheEntry<T>& entry = it->second;
		// use_count が 1 = キャッシュ以外に誰も持っていない
		const bool isUnused = entry.data.use_count() == 1 && !entry.isPersistent;
		const bool isFromPreviousScene = releaseBeforeSerial != 0 && entry.sceneSerial < releaseBeforeSerial;
		if (isUnused && (isReleaseAllRequested || isFromPreviousScene)) {
			// Model が消えると持っていた画像の使用数も減る（画像は TextureManager::Update で解放される）
			it = cache.erase(it);
		} else {
			++it;
		}
	}
}
