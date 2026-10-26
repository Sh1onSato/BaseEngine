#pragma once
#include "BaseScene.h"
#include "Model.h"
#include "SkinnedObject3d.h"
#include "Object3d.h"
#include "Camera.h"
#include "Structs.h"
#include "GltfTypes.h"
#include "Text.h"
#include <memory>
#include <string>
#include <vector>

/// <summary>
/// スケルタルアニメーション フェーズ3（アニメーション再生システム）の検証用シーン。
/// フェーズ1（スキニング描画パイプライン）・フェーズ2（自前glTFローダー）で用意した土台を統合し、
/// ModelManager::LoadGltfModel()で読み込んだGltfModelDataをSkinnedObject3dに渡すことで、
/// glTFのアニメーションデータ駆動で棒が曲がる様子を確認する
/// （フェーズ1で使っていた手動sin波ボーンパレットは廃止した）。
/// 手前の列に左から テスト用の棒 / CesiumMan / RiggedFigure / Fox / BoxTextured（Khronos の見本、Resources/gltf/khronos/）、
/// 奥の列に 車輪が回る CesiumMilkTruck と、ModelManager::LoadModel で読んだ動かない CesiumMan を並べる。
/// 奥の列の真ん中は モーフターゲットで形が変わる AnimatedMorphCube。
/// [Space] キツネのアニメーション切り替え、[L] ループ/1回、[↑][↓] 速さ、[M] モーフの重みを自分で決める。
/// カメラは右ドラッグ・ホイール・WASDQE・[R]。
/// </summary>
class SkinningTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // glTF 1つ分の表示物
    struct GltfEntry {
        // 画面に出す名前
        std::string label;
        // 動かない表示は ModelManager::LoadModel の物を共有する（動く物は SkinnedObject3d が自分で持つ）
        std::shared_ptr<Model> model;
        // 骨かアニメーションがあれば SkinnedObject3d、無ければ Object3d
        std::unique_ptr<Object3d> object;
        // object が SkinnedObject3d ならそれ（同じ物を指す。動かない表示は nullptr）
        SkinnedObject3d* skinned = nullptr;
    };

    // 影を受けるための床メッシュ（非スキニング。デフォルトボーンパレットのフォールバック確認も兼ねる）
    std::vector<VertexData> BuildFloorVertices() const;
    // glTF を読み込んで position に scale 倍で置く（骨かアニメーションがあれば動かす）。作った物を返す
    GltfEntry& AddGltf(const std::string& label, const std::string& directory, const std::string& file, const Vector3& position, float scale);
    // ModelManager::LoadModel で読んで動かさずに置く
    GltfEntry& AddStaticModel(const std::string& label, const std::string& directory, const std::string& file, const Vector3& position, float scale);
    // アニメーションの切り替え・ループ・速さのキー操作
    void UpdateAnimationControl();
    // 頭に付いてくる箱を、キツネの頭の骨の位置へ動かす
    void UpdateHeadMarker();
    std::string BuildStatusText() const;

    std::vector<GltfEntry> entries;
    // キツネ（アニメーション3本。[Space] で切り替える）
    SkinnedObject3d* fox = nullptr;
    // モーフで形が変わる箱（[M] で重みを自分で決める）
    SkinnedObject3d* morphCube = nullptr;
    // 0 = アニメーションのまま、1〜 = その番号のターゲットだけ重み 1
    size_t morphOverrideMode = 0;
    // キツネの頭に付いてくる箱（SkinnedObject3d::GetJointWorldMatrix の確認用）
    std::unique_ptr<Object3d> headMarker;
    std::shared_ptr<Model> cubeModel;
    float animationSpeed = 1.0f;

    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
    std::unique_ptr<Model> floorModel;
    std::unique_ptr<Object3d> floorObject;

    // 見る点の周りを回るカメラの操作（右ドラッグで回す・ホイールで近づく・WASD/QE で見る点を動かす・R で戻す）
    void UpdateCameraControl();
    void ResetCamera();

    // CameraManagerが所有するため非所有ポインタ
    Camera* camera = nullptr;
    // カメラが見る点と、そこからの向き・距離
    Vector3 orbitTarget = {};
    float orbitYaw = 0.0f;
    float orbitPitch = 0.0f;
    float orbitDistance = 0.0f;
};
