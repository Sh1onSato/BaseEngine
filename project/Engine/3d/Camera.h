#pragma once
#include "Calculation.h"
#include "Structs.h"
#include "WinApp.h"

class Camera {
public:
    Camera();
    void Update();
    void ImGui();

    // Getter
    const Transform& GetTransform() const { return transform; }
    Transform& GetTransform() { return transform; }
    const Matrix4x4& GetViewMatrix() const { return viewMatrix; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix; }
    const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix; }

    // Setter
    void SetRotate(const Vector3& rotate) { transform.rotate = rotate; }
    void SetTranslate(const Vector3& translate) { transform.translate = translate; }
    void SetTransform(const Transform& transform) { this->transform = transform; }

private:
    Transform transform;
    Matrix4x4 viewMatrix;
    Matrix4x4 projectionMatrix;
    Matrix4x4 viewProjectionMatrix;

    float fovY = 1.0471975f; // 60度（汎用的な既定値。狭すぎる/広すぎる場合は呼び出し側で調整）
    float aspectRatio = static_cast<float>(WinApp::KclientWidth) / static_cast<float>(WinApp::KclientHeight);
    float nearZ = 0.1f;
    float farZ = 1000.0f; // Skyboxの既定スケール(500、Skybox.cpp参照)より外側に余裕を持たせた値
};
