#pragma once
#include "GameObject.h"
#include "Structs.h"
#include <memory>

class Model;
class ModelBatcher;
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
/// 影を落とすときは、シーンが Object3dCommon::PreDrawShadow と PostDrawShadow の間で objects.DrawShadow(lightViewProjection) を呼ぶ。
/// GameObjectManager の Draw / DrawShadow から描かれるときは、同じモデルの物どうしが自動で1回の描画命令にまとめられる
/// （ModelBatcher。見た目は同じ）。まとめたくない物は SetBatchingEnabled(false)。
/// </summary>
class ModelObject : public GameObject {
public:
    ModelObject();
    ~ModelObject() override;

    void Draw() override;
    // シャドウパス用（Object3dCommon::PreDrawShadow と PostDrawShadow の間で呼ぶ）
    void DrawShadow(const Matrix4x4& lightViewProjection) override;

    // 自動まとめ描きに入れるか（既定 true）
    void SetBatchingEnabled(bool enabled) { isBatchingEnabled = enabled; }
    bool IsBatchingEnabled() const { return isBatchingEnabled; }

    // ModelManager のモデル（持っている間は解放されない）
    void SetModel(std::shared_ptr<Model> newModel);
    // 自分で作って持っているモデル（借りるだけ。この ModelObject より先に消さないこと）
    void SetModel(Model* newModel);
    Model* GetModel() const { return model; }
    void SetColor(const Vector4& newColor) { color = newColor; }
    const Vector4& GetColor() const { return color; }

private:
    // transform と色を Object3d に反映して行列を計算する（Object3d が無ければ作る）
    void SyncObject3d();
    // 今まとめ描きを受け付けているまとめ役（まとめないなら nullptr）
    ModelBatcher* GetActiveBatcher() const;
    bool isBatchingEnabled = true;

    std::unique_ptr<Object3d> object3d;
    Model* model = nullptr;
    // ModelManager のモデルを渡されたときだけ持つ（Object3d には借りる形で渡す）
    std::shared_ptr<Model> modelHolder;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};
