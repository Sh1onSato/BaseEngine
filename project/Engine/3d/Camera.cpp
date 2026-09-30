#include "Camera.h"
#include "externals/imgui/imgui.h"

Camera::Camera() {
    // 初期値設定
    transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };
    Update();
}

void Camera::Update() {
    // カメラのワールド行列を作成
    Matrix4x4 cameraMatrix = Calculation::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    // ビュー行列はカメラ行列の逆行列
    viewMatrix = Calculation::Inverse(cameraMatrix);
    // プロジェクション行列の作成
    projectionMatrix = Calculation::MakePerspectiveFovMatrix(fovY, aspectRatio, nearZ, farZ);
    // ビュープロジェクション行列の計算
    viewProjectionMatrix = viewMatrix * projectionMatrix;
}

void Camera::ImGui() {
    if (ImGui::TreeNode("Camera Settings")) {
        ImGui::DragFloat3("Position", &transform.translate.x, 0.1f);
        ImGui::DragFloat3("Rotation", &transform.rotate.x, 0.01f);
        ImGui::SliderAngle("FOV", &fovY, 10.0f, 120.0f); // 角度で指定できるように
        ImGui::DragFloat("NearClip", &nearZ, 0.01f, 0.01f, 10.0f);
        ImGui::DragFloat("FarClip", &farZ, 1.0f, 10.0f, 1000.0f);
        ImGui::TreePop();
    }
}
