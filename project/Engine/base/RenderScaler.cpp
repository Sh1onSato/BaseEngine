#include "RenderScaler.h"
#include "Logger.h"
#include "Screen.h"
#include "ShaderCompiler.h"
#include <algorithm>
#include <cmath>

namespace {
    // 3D の描画先の色の形式（Object3d などの PSO の RTVFormats と同じにする）
    constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 深度を絵として読むときの形式（深度の 24 ビットだけ読む）
    constexpr DXGI_FORMAT kDepthReadFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    void Transition(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        commandList->ResourceBarrier(1, &barrier);
    }
}

void RenderScaler::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    this->dxCommon = dxCommon;
    this->srvManager = srvManager;
    srvIndex = srvManager->Allocate();
    depthSrvIndex = srvManager->Allocate();
    CreatePipeline();
}

void RenderScaler::SetScale(float newScale) {
    scale = std::clamp(newScale, kMinScale, kMaxScale);
}

void RenderScaler::CreatePipeline() {
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
    Logger::CheckHResult(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)), "RenderScaler: DxcCreateInstance(DxcUtils)");
    Logger::CheckHResult(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)), "RenderScaler: DxcCreateInstance(DxcCompiler)");
    Logger::CheckHResult(dxcUtils->CreateDefaultIncludeHandler(&includeHandler), "RenderScaler: CreateDefaultIncludeHandler");
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShader = ShaderCompiler::CompileShader(
        L"Resources/shaders/RenderScale.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShader = ShaderCompiler::CompileShader(
        L"Resources/shaders/RenderScale.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    if (!vertexShader || !pixelShader) {
        Logger::Fatal("RenderScaler: failed to compile shaders");
    }

    // ルートシグネチャ: [0] t0 の絵、[1] b0 の定数1つ（くっきりにするか）。s0 なめらか / s1 くっきり（端は伸ばす）
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues = 1;
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    const D3D12_FILTER filters[2] = { D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_POINT };
    for (uint32_t i = 0; i < 2; ++i) {
        samplers[i].Filter = filters[i];
        samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.pParameters = parameters;
    rootDesc.NumParameters = _countof(parameters);
    rootDesc.pStaticSamplers = samplers;
    rootDesc.NumStaticSamplers = _countof(samplers);
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr) && errorBlob) {
        Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
    }
    Logger::CheckHResult(hr, "RenderScaler: D3D12SerializeRootSignature");
    hr = dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    Logger::CheckHResult(hr, "RenderScaler: CreateRootSignature");

    // 頂点バッファなし（頂点番号から作る）・深度なし・ブレンドなし
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature.Get();
    desc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    desc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kColorFormat;
    // バックバッファと一緒に深度バッファも設定されているので、形式だけ合わせる（深度は使わない）
    desc.DSVFormat = kDepthFormat;
    desc.DepthStencilState.DepthEnable = false;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
    Logger::CheckHResult(hr, "RenderScaler: CreateGraphicsPipelineState");
}

void RenderScaler::EnsureTarget(uint32_t width, uint32_t height) {
    if (colorResource && width == targetWidth && height == targetHeight) {
        return;
    }
    // 前のフレームの描画は PostDraw で GPU の完了を待っているので、古い描画先はすぐ捨ててよい
    ID3D12Device* device = dxCommon->GetDevice();

    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = width;
    colorDesc.Height = height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = kColorFormat;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kColorFormat;
    std::copy(std::begin(DirectXCommon::kClearColor), std::end(DirectXCommon::kClearColor), clearValue.Color);
    // 普段は「絵として読む」状態にしておき、描くときだけ描画先の状態にする
    HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &colorDesc,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&colorResource));
    Logger::CheckHResult(hr, "RenderScaler: CreateCommittedResource(color)");

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = kColorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(colorResource.Get(), &rtvDesc, dxCommon->GetRtvHandle(kRtvIndex));
    srvManager->CreateSRVForTexture2D(srvIndex, colorResource.Get(), kColorFormat, 1);

    // 深度: 画面の効果（SSAO）で絵として読めるよう、形式を決めずに作り（TYPELESS）、深度として書く・数値として読むの両方の見方を作る
    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.Format = kDepthFormat;
    depthClearValue.DepthStencil.Depth = 1.0f;
    hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                         &depthClearValue, IID_PPV_ARGS(&depthResource));
    Logger::CheckHResult(hr, "RenderScaler: CreateCommittedResource(depth)");
    srvManager->CreateSRVForTexture2D(depthSrvIndex, depthResource.Get(), kDepthReadFormat, 1);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = kDepthFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depthResource.Get(), &dsvDesc,
        DirectXCommon::GetCPUDescriptorHandle(dxCommon->GetDsvHeap(), dxCommon->GetDescriptorSizeDSV(), kDsvIndex));

    targetWidth = width;
    targetHeight = height;
}

void RenderScaler::BeginScene() {
    const D3D12_VIEWPORT& screenViewport = Screen::GetViewport();
    // 倍率が 1 でも、画面の効果を掛けるフレームは別の描画先に描いてから写す
    const bool isScaled = scale < kMaxScale;
    isOffscreen = isScaled || (postProcess && postProcess->IsActive());
    if (!isOffscreen) {
        renderWidth = static_cast<uint32_t>(screenViewport.Width);
        renderHeight = static_cast<uint32_t>(screenViewport.Height);
        return;
    }

    // ゲームの絵の大きさ × 倍率 の描画先に描く（縦横比は変わらないので、カメラ・スプライトの座標はそのまま）
    const float appliedScale = isScaled ? scale : kMaxScale;
    renderWidth = (std::max)(1u, static_cast<uint32_t>(std::lround(screenViewport.Width * appliedScale)));
    renderHeight = (std::max)(1u, static_cast<uint32_t>(std::lround(screenViewport.Height * appliedScale)));
    EnsureTarget(renderWidth, renderHeight);

    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    Transition(commandList, colorResource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon->GetRtvHandle(kRtvIndex);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = DirectXCommon::GetCPUDescriptorHandle(dxCommon->GetDsvHeap(), dxCommon->GetDescriptorSizeDSV(), kDsvIndex);
    commandList->ClearRenderTargetView(rtv, DirectXCommon::kClearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(renderWidth);
    viewport.Height = static_cast<float>(renderHeight);
    viewport.MaxDepth = 1.0f;
    const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(renderWidth), static_cast<LONG>(renderHeight) };
    dxCommon->SetMainRenderTarget(rtv, dsv, viewport, scissor);
}

void RenderScaler::EndScene() {
    if (!isOffscreen) {
        return;
    }
    isOffscreen = false;

    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    Transition(commandList, colorResource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // バックバッファのゲームの絵の範囲（黒帯の内側）に引き伸ばして描く
    dxCommon->ResetMainRenderTarget();
    if (postProcess && postProcess->IsActive()) {
        // 画面の効果を掛けながら写す（引き伸ばしも PostProcess の最後の段で行う）。SSAO を使うなら深度を絵として読める状態にする
        const bool usesDepth = postProcess->UsesDepth();
        if (usesDepth) {
            Transition(commandList, depthResource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        postProcess->Render(srvIndex, renderWidth, renderHeight, filter == Filter::kPoint, usesDepth ? depthSrvIndex : PostProcess::kNoDepth);
        if (usesDepth) {
            Transition(commandList, depthResource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
        return;
    }
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->SetPipelineState(pipelineState.Get());
    ID3D12DescriptorHeap* heaps[] = { srvManager->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(srvIndex));
    commandList->SetGraphicsRoot32BitConstant(1, filter == Filter::kPoint ? 1u : 0u, 0);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}
