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

    void SetModel(Model* model) { this->model = model; }
    void SetCamera(Camera* camera) { this->camera = camera; }

private:
    SkyboxCommon* common = nullptr;
    Model* model = nullptr;
    Camera* camera = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
    TransformationMatrix* wvpData = nullptr;

    uint32_t textureIndex = 0;
    Transform transform;
};
