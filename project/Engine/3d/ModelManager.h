#pragma once
#include "Model.h"
#include "GltfTypes.h"
#include <cstdint>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <memory>

/// <summary>
/// モデルの読み込みと使い回し、使われなくなったモデルの解放をするクラス。
/// 返すモデルは shared_ptr。持っている人（Object3d・ModelObject・ModelInstancer など）がいる間は解放されない。
/// 誰も持っていないモデルは次の時に解放される（実際に消すのはフレームの始めの Update）:
///   - シーンが切り替わって、新しいシーンの Initialize で使われなかったとき
///   - ReleaseUnused() を呼んだとき
/// 立方体・球（GetCubeModel / GetSphereModel）は解放しない。
/// </summary>
class ModelManager {
public:
	static ModelManager* GetInstance();

	// 初期化
	void Initialize(DirectXCommon* dxCommon);

	// 終了
	void Finalize();

	// モデルのロード（すでに読み込み済みならそれを返す）
	std::shared_ptr<Model> LoadModel(const std::string& directoryPath, const std::string& filename);

	// 1辺1・中心が原点の白い立方体（ファイル不要。初回に作って使い回す）。色は Object3d::SetColor で付ける
	std::shared_ptr<Model> GetCubeModel();
	// 半径0.5・中心が原点の白い球（GetCubeModel と同じく使い回す）
	std::shared_ptr<Model> GetSphereModel();

	// glTFモデルのロード（すでに読み込み済みならそれを返す）。SkinnedObject3d::SetGltfData()に渡して使う。
	std::shared_ptr<GltfModelData> LoadGltfModel(const std::string& directoryPath, const std::string& filename);

	// 誰も持っていないモデル（立方体・球を除く）を、次のフレームの始めに全部解放する
	void ReleaseUnused();
	// シーンが切り替わった（SceneManager が新しいシーンの Initialize の前に呼ぶ）
	void OnSceneChanged();
	// フレームの始めに呼ぶ（Framework）。解放を予約されたモデルを実際に解放する。
	// モデルが消えると画像の使用数が減るので、TextureManager::Update より前に呼ぶ
	void Update();

	// 読み込んでいるモデルの数（glTF も含む。デバッグ表示用）
	size_t GetLoadedCount() const { return models.size() + gltfModels.size(); }

private:
	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;

	// キャッシュ1件分
	template <typename T>
	struct CacheEntry {
		std::shared_ptr<T> data;
		// 最後に読み込まれた（使われた）シーンの番号
		uint64_t sceneSerial = 0;
		// true なら解放しない（立方体・球）
		bool isPersistent = false;
	};

	// 頂点から作るモデルを key でキャッシュする（初回だけ buildVertices を呼ぶ）
	std::shared_ptr<Model> GetPrimitiveModel(const std::string& key, const std::function<std::vector<VertexData>()>& buildVertices);
	// cache の中で、解放してよい物を消す
	template <typename T>
	void ReleaseFrom(std::map<std::string, CacheEntry<T>>& cache);

	DirectXCommon* dxCommon = nullptr;

	// モデルのキャッシュ
	std::map<std::string, CacheEntry<Model>> models;
	// glTFモデルのキャッシュ
	std::map<std::string, CacheEntry<GltfModelData>> gltfModels;

	// 今のシーンの番号（OnSceneChanged で増える）
	uint64_t currentSceneSerial = 0;
	// この番号より前のシーンで使われた、誰も持っていないモデルを次の Update で解放する（0 なら予約なし）
	uint64_t releaseBeforeSerial = 0;
	bool isReleaseAllRequested = false;
};
