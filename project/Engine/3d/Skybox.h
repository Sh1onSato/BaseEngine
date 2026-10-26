#pragma once
#include "SkyboxCommon.h"
#include "Model.h"
#include "Camera.h"
#include "Calculation.h"
#include <memory>
#include <string>

class Skybox {
public:
    void Initialize(SkyboxCommon* common, const std::string& textureFilePath);
    void Update();
    void Draw();

    // ModelManager のモデル（持っている間は解放されない）
    void SetModel(std::shared_ptr<Model> model) { modelHolder = std::move(model); this->model = modelHolder.get(); }
    // 自分で作って持っているモデル（借りるだけ。この Skybox より先に消さないこと）
    void SetModel(Model* model) { modelHolder.reset(); this->model = model; }
    void SetCamera(Camera* camera) { this->camera = camera; }

private:
    SkyboxCommon* common = nullptr;
    Model* model = nullptr;
    // ModelManager のモデルを渡されたときだけ持つ（解放されないように）
    std::shared_ptr<Model> modelHolder;
    Camera* camera = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
    TransformationMatrix* wvpData = nullptr;

    // 使っているキューブマップ（持っている間は解放されない）
    TextureHandle texture;
    Transform transform;
};
