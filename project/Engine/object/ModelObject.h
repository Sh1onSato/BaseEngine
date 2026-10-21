#pragma once
#include "GameObject.h"
#include "Structs.h"
#include <memory>

class Model;
class Object3d;

/// <summary>
/// 3Dモデルを1つ表示する GameObject。描画のたびに transform を Object3d へ反映する
/// （当たり判定の押し戻しは Update の後なので、描画時に反映すれば押し戻し後の位置が見える）。
/// レベルデータで登録されていない "MESH" はこのクラスで作られる。
/// 継承して敵などを作ってもよい（Draw を override したら ModelObject::Draw() も呼ぶこと）。
///
/// 使い方:
///   ModelObject* rock = objects.Create<ModelObject>();
///   rock->SetModel(ModelManager::GetInstance()->LoadModel("Resources/models/rock", "rock.obj"));
///   rock->transform.translate = { 0.0f, 0.0f, 5.0f };
/// 影を落とすときは、シーンが Object3dCommon::PreDrawShadow と PostDrawShadow の間で DrawShadow() を呼ぶ。
/// </summary>
class ModelObject : public GameObject {
public:
    ModelObject();
    ~ModelObject() override;

    void Draw() override;
    // シャドウパス用（Object3dCommon::PreDrawShadow と PostDrawShadow の間で呼ぶ）
    void DrawShadow(const Matrix4x4& lightViewProjection);

    // model は ModelManager などが持つ（借りるだけ）
    void SetModel(Model* newModel);
    Model* GetModel() const { return model; }
    void SetColor(const Vector4& newColor) { color = newColor; }
    const Vector4& GetColor() const { return color; }

private:
    // transform と色を Object3d に反映して行列を計算する（Object3d が無ければ作る）
    void SyncObject3d();

    std::unique_ptr<Object3d> object3d;
    Model* model = nullptr;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};
