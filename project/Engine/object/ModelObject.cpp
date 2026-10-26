#include "ModelObject.h"
#include "Framework.h"
#include "Model.h"
#include "Object3d.h"

ModelObject::ModelObject() = default;
// Object3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く（unique_ptr<Object3d> の破棄に必要）
ModelObject::~ModelObject() = default;

void ModelObject::SetModel(std::shared_ptr<Model> newModel) {
    // 先に借りる側を差し替えてから持つ（同じモデルを渡し直しても消えないように）
    SetModel(newModel.get());
    modelHolder = std::move(newModel);
}

void ModelObject::SetModel(Model* newModel) {
    modelHolder.reset();
    model = newModel;
    if (object3d) {
        object3d->SetModel(model);
    }
}

void ModelObject::SyncObject3d() {
    if (!object3d) {
        object3d = std::make_unique<Object3d>();
        object3d->Initialize(Framework::GetInstance()->GetObject3dCommon());
        object3d->SetModel(model);
        // カメラは SetCamera しない → CameraManager のアクティブカメラが使われる
    }
    object3d->SetScale(transform.scale);
    object3d->SetRotate(transform.rotate);
    object3d->SetTranslate(transform.translate);
    object3d->SetColor(color);
    object3d->Update();
}

void ModelObject::Draw() {
    if (!model) {
        return;
    }
    SyncObject3d();
    object3d->Draw();
}

void ModelObject::DrawShadow(const Matrix4x4& lightViewProjection) {
    if (!model) {
        return;
    }
    // シャドウパスは通常の Draw より先なので、ここで先に最新の位置を反映しておく
    SyncObject3d();
    object3d->DrawShadow(lightViewProjection);
}
