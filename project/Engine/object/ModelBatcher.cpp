#include "ModelBatcher.h"
#include "Model.h"
#include "ModelInstancer.h"

namespace {
    // モデル1種類あたり最初に用意する数（足りなければ ModelInstancer が自動で広げる）
    constexpr size_t kInitialCapacity = 16;
}

ModelBatcher::ModelBatcher() = default;
// ModelInstancer の中身はヘッダーで見えないので、デストラクタは .cpp に置く（unique_ptr<ModelInstancer> の破棄に必要）
ModelBatcher::~ModelBatcher() = default;

void ModelBatcher::Begin(bool shadowPass) {
    isActive = true;
    isShadowPass = shadowPass;
    usedModels.clear();
    objectCount = 0;
}

void ModelBatcher::Add(Model* model, const Transform& transform, const Vector4& color) {
    if (!isActive || !model) {
        return;
    }
    Group& group = (isShadowPass ? shadowGroups : mainGroups)[model];
    if (!group.instancer) {
        group.instancer = std::make_unique<ModelInstancer>();
        group.instancer->Initialize(model, kInitialCapacity);
    }
    if (!group.isUsed) {
        // このパスで初めて使うモデル: 前のフレームの分を消してから足していく
        group.isUsed = true;
        group.instancer->Clear();
        group.instancer->SetModel(model);
        usedModels.push_back(model);
    }
    group.instancer->Add(transform, color);
    ++objectCount;
}

void ModelBatcher::Flush() {
    if (!isActive) {
        return;
    }
    GroupMap& groups = isShadowPass ? shadowGroups : mainGroups;
    for (Model* model : usedModels) {
        ModelInstancer& instancer = *groups[model].instancer;
        if (isShadowPass) {
            instancer.DrawShadow();
        } else {
            instancer.Draw();
        }
    }
    if (!isShadowPass) {
        lastObjectCount = objectCount;
        lastDrawCallCount = usedModels.size();
    }

    // このパスで使われなかったモデルの分は捨てる（消えたモデルを持ち続けないように）
    for (auto it = groups.begin(); it != groups.end();) {
        if (it->second.isUsed) {
            it->second.isUsed = false;
            ++it;
        } else {
            it = groups.erase(it);
        }
    }
    usedModels.clear();
    isActive = false;
}
