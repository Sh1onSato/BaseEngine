#include "DirectXCommon.h"
#include "SrvManager.h"
#include <cassert>
#include "Logger.h"
#include "StringUtility.h"
#include <thread>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib") // IID_PPV_ARGS等の解決に必要

using namespace Microsoft::WRL;
using namespace Logger;
using namespace StringUtility;

void DirectXCommon::Initialize(WinApp* winApp) {
    this->winApp = winApp;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    assert(SUCCEEDED(hr));

#ifdef _DEBUG
    ComPtr<ID3D12Debug1> debugController = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif

    // アダプターの選定
    for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdpter)) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdpter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr));
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            Log(ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));
            break;
        }
        useAdpter = nullptr;
    }
    assert(useAdpter != nullptr);

    // デバイス生成
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    for (size_t i = 0; i < _countof(featureLevels); i++) {
        hr = D3D12CreateDevice(useAdpter.Get(), featureLevels[i], IID_PPV_ARGS(&device));
        if (SUCCEEDED(hr)) break;
    }
    assert(device != nullptr);

#ifdef _DEBUG
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        D3D12_MESSAGE_ID denyIds[] = { D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE };
        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        infoQueue->PushStorageFilter(&filter);
    }
#endif

    // コマンド系作成
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

    // スワップチェーン
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = WinApp::KclientWidth;
    swapChainDesc.Height = WinApp::KclientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), winApp->GetHwnd(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // ヒープ作成
    rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxRtvCount, false);
    dsvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kMaxDsvCount, false);

    // RTV作成
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CreateSwapChainRTV();

    // DSV作成
    depthStencilResource = CreateDepthStencilTexureResource(WinApp::KclientWidth, WinApp::KclientHeight);
    
    // DSVの設定
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
    device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // 同期・描画設定
    device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    viewport.Width = (float)WinApp::KclientWidth;
    viewport.Height = (float)WinApp::KclientHeight;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = WinApp::KclientWidth;
    scissorRect.bottom = WinApp::KclientHeight;


    // 1. ルートパラメータ（シェーダーへの窓口）の設定
    D3D12_ROOT_PARAMETER rootParameters[4] = {};

    // マテリアル
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // WVP
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 1;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // テクスチャ
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].BaseShaderRegister = 0; // t0
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // ライト
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].Descriptor.ShaderRegister = 2;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // サンプラー（テクスチャの補間設定）
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0; // s0
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // RootSignatureの作成
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.pParameters = rootParameters;               // パラメータ配列の先頭
    descriptionRootSignature.NumParameters = _countof(rootParameters);     // パラメータの数（4つ）
    descriptionRootSignature.pStaticSamplers = staticSamplers;           // サンプラー配列の先頭
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers); // サンプラーの数（1つ）
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // シリアライズして生成
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(error->GetBufferPointer()));
        assert(false);
    }

    device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

    InitialaizeFixFPS();
}

void DirectXCommon::PreDraw(){
    // バックバッファの番号を取得
    UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

    // リソースバリアを Present から RenderTarget へ
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);

    // 描画先の設定（RTV & DSV）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += backBufferIndex * rtvDescriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // 画面全体をクリア
    commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
    // 深度とステンシルの両方をクリア
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // 共通の描画設定
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PostDraw(){
    UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

    // リソースバリアをRenderTargetからPresent
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList->ResourceBarrier(1, &barrier);

    // コマンドリストを閉じて実行
    HRESULT hr = commandList->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);

    // 画面の入れ替え
    swapChain->Present(1, 0);

    // GPUの完了を待機
    WaitForGpu();

    UpdateFixFPS();

    // 次のフレーム用の準備
    hr = commandAllocator->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList->Reset(commandAllocator.Get(), nullptr);
    assert(SUCCEEDED(hr));
}

void DirectXCommon::WaitForGpu(){
    fenceValue++;
    commandQueue->Signal(fence.Get(), fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        // イベントをセットして待機
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void DirectXCommon::CreateSwapChainRTV(){
    // RTVの設定（Desc）
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // PSOと統一
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < 2; i++) {
        // スワップチェーンからバッファを取得
        HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
        assert(SUCCEEDED(hr));

        // 書き込むハンドルの場所を計算
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvStartHandle;
        handle.ptr += i * rtvDescriptorSize;

        // RTVの生成
        device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, handle);
    }
}

// ヘルパー関数群（引数にdeviceを取らない形に整理）
ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
    ComPtr<ID3D12DescriptorHeap> heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = heapType;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    return heap;
}

ComPtr<ID3D12Resource> DirectXCommon::CreateDepthStencilTexureResource(int32_t width, int32_t height) {
    D3D12_RESOURCE_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.DepthOrArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES prop{};
    prop.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clear{};
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;
    clear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    ComPtr<ID3D12Resource> res = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &prop,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&res)
    );
    assert(SUCCEEDED(hr));
    return res;
}

ComPtr<ID3D12Resource> DirectXCommon::CreateShadowMapResource(int32_t width, int32_t height) {
    D3D12_RESOURCE_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.DepthOrArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES prop{};
    prop.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clear{};
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;
    clear.Format = DXGI_FORMAT_D32_FLOAT;

    ComPtr<ID3D12Resource> res = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &prop,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear,
        IID_PPV_ARGS(&res)
    );
    assert(SUCCEEDED(hr));
    return res;
}

ComPtr<ID3D12Resource> DirectXCommon::CreatBufferResource(size_t sizeInBytes) {
    // 定数バッファは256バイトの倍数である必要があるため、切り上げ計算を行う
    size_t alignment = 256;
    size_t alignedSize = (sizeInBytes + alignment - 1) & ~(alignment - 1);

    D3D12_HEAP_PROPERTIES prop{};
    prop.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = alignedSize; // 切り上げたサイズを指定
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> res = nullptr;
    device->CreateCommittedResource(
        &prop,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&res)
    );
    return res;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages, DirectXCommon* dxCommon){
    // Mate情報を取得
    std::vector<D3D12_SUBRESOURCE_DATA>subresorce;
    DirectX::PrepareUpload(dxCommon->GetDevice(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresorce); // 変更点: device.Get() を使用
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresorce.size()));
    ComPtr <ID3D12Resource> intermediateResource = dxCommon->CreatBufferResource(intermediateSize); // 変更点: CreatBufferResouce に device.Get() を渡す
    UpdateSubresources(dxCommon->GetCommandList(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresorce.size()), subresorce.data()); // 変更点: texture.Get() と intermediateResource.Get() を使用

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    dxCommon->GetCommandList()->ResourceBarrier(1, &barrier);
    return intermediateResource;
}

void DirectXCommon::InitialaizeFixFPS(){
	reference_ = std::chrono::steady_clock::now();
}

void DirectXCommon::UpdateFixFPS(){
    // 1/60秒ぴったりの時間
	const std::chrono::microseconds KMinTime(uint64_t(1000000.0f / 60.0f));
    // 1/60秒よりわずかに短い時間
	const std::chrono::microseconds KMinCheckTime(uint64_t(1000000.0f / 65.0f));

    // 現在時間を取得する
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    // 前回記録から経過時間取得する
    std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

    // 1/60(よわずかに短い時間)立っていない場合
    if (elapsed < KMinTime) {
        // 1/60秒経過するまで微小なスリープを繰り返す
        while (std::chrono::steady_clock::now() - reference_ < KMinTime) {
			// 1マイクロ秒スリープ
			std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
	reference_ = std::chrono::steady_clock::now();
}

// スワップチェーン(2) + PostProcessのオフスクリーンRTV(現状6枚、インデックス2-7)で計8枚使用。将来のパス追加分の余裕を持たせた値
const uint32_t DirectXCommon::kMaxRtvCount = 16;
// メイン深度(1) + シャドウマップ(1)で計2枚使用。将来の複数シャドウマップ等に備えた余裕を持たせた値
const uint32_t DirectXCommon::kMaxDsvCount = 8;
