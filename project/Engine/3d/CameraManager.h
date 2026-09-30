#pragma once
#include "Camera.h"
#include <map>
#include <string>
#include <memory>

/// <summary>
/// カメラマネージャー
/// </summary>
class CameraManager {
public:
    static CameraManager* GetInstance();

    void Initialize();

    // カメラの追加
    void AddCamera(const std::string& name, std::unique_ptr<Camera> camera);

    // アクティブなカメラの設定
    void SetActiveCamera(const std::string& name);

    // アクティブなカメラの取得
    Camera* GetActiveCamera() const;

    // 指定した名前のカメラを取得
    Camera* GetCamera(const std::string& name) const;

private:
    CameraManager() = default;
    ~CameraManager() = default;
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    // カメラのコンテナ
    std::map<std::string, std::unique_ptr<Camera>> cameras;
    // 現在アクティブなカメラの名前
    std::string activeCameraName;

    static CameraManager* instance;
};
