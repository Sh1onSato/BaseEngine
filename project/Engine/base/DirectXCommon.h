#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl.h>
#include <string>
#include <format>
#include <stdint.h>
#include"externals/DirectXTex/DirectXTex.h"
#include"externals/DirectXTex/d3dx12.h"
#include "WinApp.h"
#include <chrono>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

// D3Dリソースリークチェック用の構造体
struct D3DResourceLeakCheker {
    ~D3DResourceLeakCheker() {
        Microsoft::WRL::ComPtr <IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

class DirectXCommon {
public:
    // --- スタティックヘルパー関数 ---
    static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
        D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handleCPU.ptr += static_cast<SIZE_T>(descriptorSize) * index;
        return handleCPU;
    }

    static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
        D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        handleGPU.ptr += static_cast<SIZE_T>(descriptorSize) * index;
        return handleGPU;
    }

    // 初期化
    void Initialize(WinApp* winApp);
    void PreDraw();
    void PostDraw();

    void WaitForGpu();
	void CreateSwapChainRTV();

    // --- ゲッター ----
    // --- デバイス・コマンド系 ---
    ID3D12Device* GetDevice() const { return device.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue.Get(); }
    ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator.Get(); }

    // --- ヒープ系 ---
    ID3D12DescriptorHeap* GetRtvHeap() const { return rtvDescriptorHeap.Get(); }
    ID3D12DescriptorHeap* GetDsvHeap() const { return dsvDescriptorHeap.Get(); }

    // --- 同期系 ---
    ID3D12Fence* GetFence() const { return fence.Get(); }
    UINT64 GetFenceValue() const { return fenceValue; }
    HANDLE GetFenceEvent() const { return fenceEvent; }

    // --- 設定・情報取得系 ---
    // 現在のバックバッファのインデックスを取得
    UINT GetBackBufferIndex() const { return swapChain->GetCurrentBackBufferIndex(); }
    // バックバッファの数を取得（通常は2）
    size_t GetBackBufferCount() const { return _countof(swapChainResources); }
    // SRV等のデスクリプタ1個分のサイズを取得（ズレを防ぐために重要）
    UINT GetDescriptorSizeSRV() const { return device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); }
    UINT GetDescriptorSizeRTV() const { return device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); }
    UINT GetDescriptorSizeDSV() const { return device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV); }

    // --- 表示系 ---
    IDXGISwapChain4* GetSwapChain() const { return swapChain.Get(); }
    ID3D12Resource* GetSwapChainResource(UINT index) const { return swapChainResources[index].Get(); }

    // -- - ハンドル取得系-- -
    // RTV(レンダーターゲット)のハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle(UINT index) const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += (index * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
        return handle;
    }

    // DSV(深度バッファ)のハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const {
        return dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    }

    // 深度バッファのリソースを返す
    ID3D12Resource* GetDepthStencilResource() const { return depthStencilResource.Get(); }

    // --- ビューポート・シザー矩形 ---
const D3D12_VIEWPORT& GetViewport() const { return viewport; }
const D3D12_RECT& GetScissorRect() const { return scissorRect; }

    // --- 暫定保持用 ---
    ID3D12RootSignature* GetRootSignature() const { return rootSignature.Get(); }

    // ヘルパー関数
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTexureResource(int32_t width, int32_t height);
    // シャドウマップ用の深度テクスチャを作成（定常状態はPIXEL_SHADER_RESOURCE。描画時のみDEPTH_WRITEに遷移）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateShadowMapResource(int32_t width, int32_t height);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreatBufferResource(size_t sizeInBytes);
    [[nodiscard]]
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages, DirectXCommon* dxCommon);

    // RTV/DSVヒープのサイズ上限（用途別に分離。SRV用の上限はSrvManager::kMaxSRVCountを参照）
    static const uint32_t kMaxRtvCount;
    static const uint32_t kMaxDsvCount;

    // 画面クリアカラー（背景色）。PostProcessのオフスクリーンRTVクリアもこの値を共用する
    static constexpr float kClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
private:
    // --- 基盤系 ---
    WinApp* winApp = nullptr;
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdpter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;

    // --- コマンド系 ---
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

    // --- 表示・リソース系 ---
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource;

    // --- ディスクリプタヒープ ---
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;

    // --- 同期系 ---
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    HANDLE fenceEvent = nullptr;

    // --- デバッグ系 ---
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;

    // --- 描画設定保持用（とりあえずここに置く） ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    uint32_t rtvDescriptorSize;

    // FPS固定初期化
    void InitialaizeFixFPS();
    // FPS固定更新
	void UpdateFixFPS();
    // 記録時間(FPS固定化用)
    std::chrono::steady_clock::time_point reference_;

   
};