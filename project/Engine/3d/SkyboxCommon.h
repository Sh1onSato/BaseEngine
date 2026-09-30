#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>

class SkyboxCommon {
public:
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    void PreDraw();

    DirectXCommon* GetDxCommon() const { return dxCommon; }
    SrvManager* GetSrvManager() const { return srvManager; }

private:
    void CreateRootSignature();
    void CreateGraphicsPipeline();

private:
    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
};
