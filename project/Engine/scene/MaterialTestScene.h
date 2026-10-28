#pragma once
#include "BaseScene.h"
#include "Text.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class Model;
class Object3d;

/// <summary>
/// 材質（金属・粗さ・法線マップ・金属/粗さの画像・発光）を確かめるための検証用シーン（ゲーム本編では使わない）。
/// 左: 球の並び（横 = 金属らしさ 0〜1、縦 = 粗さ 0.1 / 0.5 / 0.9。同じ球のモデルを Object3d ごとの上書きで変える）。
/// 右: 法線マップの板と回る立方体・球（光の向きで凹凸が見える）、左半分ざらざら・右半分つるつるの金属の立方体（金属/粗さの画像）、
///     光る球（画面の効果のブルームで光がにじむ）。点光源が回るので照り返しが動く。
/// [N] 法線マップ ON/OFF  [P] 今までの簡単な当たり方と比べる。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "MATERIAL_TEST" にする。
/// </summary>
class MaterialTestScene : public BaseScene {
public:
    MaterialTestScene();
    ~MaterialTestScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void SetupCamera();
    void InitializeTexts();
    void SetupSphereGrid();
    void SetupNormalMapObjects();
    void SetupOtherObjects();
    std::unique_ptr<Object3d> CreateObject(std::shared_ptr<Model> model, const Vector3& position, const Vector3& scale, const Vector4& color);
    void UpdateInput();
    void DrawShadowPass();
    std::string BuildStatusText() const;

    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;
    std::vector<std::unique_ptr<Object3d>> objects;
    // 自分で作ったモデル（共有のモデルに法線マップを付けると、他の物まで変わるため）
    std::shared_ptr<Model> normalCubeModel;
    std::shared_ptr<Model> normalSphereModel;
    std::shared_ptr<Model> metallicRoughnessCubeModel;
    Object3d* rotatingCube = nullptr;
    Object3d* rotatingSphere = nullptr;
    float elapsedTime = 0.0f;
    bool isNormalMapOn = true;
    bool isPhysicalOn = true;
    // ラベル（金属・粗さの数字）
    std::vector<std::unique_ptr<Text>> labels;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
};
