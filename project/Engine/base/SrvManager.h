#pragma once
#include "DirectXCommon.h"
#include <wrl.h>
#include <cstdint>

class SrvManager {
public:
    // 最大SRV数
    static const uint32_t kMaxSRVCount = 512;

    void Initialize(DirectXCommon* dxCommon);

    // 空きインデックスを確保してその番号を返す
    uint32_t Allocate();

    // 指定インデックスのハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

    // SRVの作成
    void CreateSRVForTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT mipLevels);
    void CreateSRVForTextureCube(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT mipLevels);
    // シャドウマップ（深度テクスチャ）用のSRVを作成（Format R32_FLOAT固定）
    void CreateSRVForDepthTexture(uint32_t srvIndex, ID3D12Resource* pResource);

    // 描画前の設定（ヒープのセット）
    void PreDraw();

    // DescriptorHeapの取得
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return descriptorHeap.Get(); }

private:
    DirectXCommon* dxCommon = nullptr;

    // SRV用ディスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    // ディスクリプタ1つのサイズ
    uint32_t descriptorSize;
    // 次に使用するインデックス
    uint32_t useCount = 0;
};
