#include "CameraManager.h"
#include <cassert>

CameraManager* CameraManager::instance = nullptr;

CameraManager* CameraManager::GetInstance() {
    if (instance == nullptr) {
        instance = new CameraManager();
    }
    return instance;
}

void CameraManager::Initialize() {
    cameras.clear();
    activeCameraName = "";
}

void CameraManager::AddCamera(const std::string& name, std::unique_ptr<Camera> camera) {
    // すでに存在する場合は上書きせずエラーにする（あるいは上書きを許容するかは設計次第）
    assert(cameras.find(name) == cameras.end());
    cameras[name] = std::move(camera);

    // 最初のカメラを自動的にアクティブにする
    if (activeCameraName.empty()) {
        activeCameraName = name;
    }
}

void CameraManager::SetActiveCamera(const std::string& name) {
    // 存在するかチェック
    if (cameras.find(name) != cameras.end()) {
        activeCameraName = name;
    }
}

Camera* CameraManager::GetActiveCamera() const {
    if (activeCameraName.empty() || cameras.find(activeCameraName) == cameras.end()) {
        return nullptr;
    }
    return cameras.at(activeCameraName).get();
}

Camera* CameraManager::GetCamera(const std::string& name) const {
    if (cameras.find(name) != cameras.end()) {
        return cameras.at(name).get();
    }
    return nullptr;
}
