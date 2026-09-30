#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"

class ImGuiManager {
public:
    static ImGuiManager* GetInstance();

    // 初期化
    void Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager);

    // フレーム開始
    void Begin();

    // フレーム終了・描画命令
    void End();

    // 描画（レンダリングの実行）
    void Draw();

    // 終了処理
    void Finalize();

private:
    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    DirectXCommon* dxCommon = nullptr;
    static ImGuiManager* instance;
};
