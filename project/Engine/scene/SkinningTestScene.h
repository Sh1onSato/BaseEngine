#pragma once
#include "BaseScene.h"
#include "Model.h"
#include "SkinnedObject3d.h"
#include "Object3d.h"
#include "Camera.h"
#include "Structs.h"
#include "GltfTypes.h"
#include <memory>

/// <summary>
/// スケルタルアニメーション フェーズ3（アニメーション再生システム）の検証用シーン。
/// フェーズ1（スキニング描画パイプライン）・フェーズ2（自前glTFローダー）で用意した土台を統合し、
/// ModelManager::LoadGltfModel()で読み込んだGltfModelDataをSkinnedObject3dに渡すことで、
/// glTFのアニメーションデータ駆動で棒が曲がる様子を確認する
/// （フェーズ1で使っていた手動sin波ボーンパレットは廃止した）。
/// </summary>
class SkinningTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // 影を受けるための床メッシュ（非スキニング。デフォルトボーンパレットのフォールバック確認も兼ねる）
    std::vector<VertexData> BuildFloorVertices() const;

    std::unique_ptr<Model> barModel;
    std::unique_ptr<Model> floorModel;
    std::unique_ptr<SkinnedObject3d> barObject;
    std::unique_ptr<Object3d> floorObject;

    // CameraManagerが所有するため非所有ポインタ
    Camera* camera = nullptr;

    // ModelManagerがキャッシュとして所有するため非所有ポインタ（Finalizeで解放してはいけない）
    GltfModelData* gltfData = nullptr;
};
