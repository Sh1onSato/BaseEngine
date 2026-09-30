#include "PostProcess.h"
#include "ShaderCompiler.h"
#include "externals/imgui/imgui.h"
#include <cassert>

using namespace ShaderCompiler;

void PostProcess::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    this->dxCommon = dxCommon;
    this->srvManager = srvManager;

    // 1. テクスチャリソースの設定（フル解像度シーン）
    D3D12_RESOURCE_DESC desc{};
    desc.Width = WinApp::KclientWidth;
    desc.Height = WinApp::KclientHeight;
    desc.MipLevels = 1;
    desc.DepthOrArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = desc.Format;
    for (int i = 0; i < 4; ++i) clearValue.Color[i] = DirectXCommon::kClearColor[i];

    HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&resource)
    );
    assert(SUCCEEDED(hr));

    // 2. RTVの作成
    rtvIndex = kSceneRtvIndex;
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dxCommon->GetDevice()->CreateRenderTargetView(resource.Get(), &rtvDesc, dxCommon->GetRtvHandle(rtvIndex));

    // 3. SRVの作成
    srvIndex = srvManager->Allocate();
    srvManager->CreateSRVForTexture2D(srvIndex, resource.Get(), desc.Format, 1);

    // 4. ブルーム用の半解像度テクスチャ3枚（輝度抽出／水平ブラー／垂直ブラー=最終ブルーム）
    brightnessResource = CreateBloomTexture(kBloomWidth, kBloomHeight, kBrightnessRtvIndex, brightnessSrvIndex);
    blurHResource = CreateBloomTexture(kBloomWidth, kBloomHeight, kBlurHRtvIndex, blurHSrvIndex);
    blurVResource = CreateBloomTexture(kBloomWidth, kBloomHeight, kBlurVRtvIndex, blurVSrvIndex);

    // 4.5. 画面全体ぼかし用のフル解像度テクスチャ2枚（水平ブラー／垂直ブラー=最終ぼかし）
    fullBlurHResource = CreateBloomTexture(WinApp::KclientWidth, WinApp::KclientHeight, kFullBlurHRtvIndex, fullBlurHSrvIndex);
    fullBlurVResource = CreateBloomTexture(WinApp::KclientWidth, WinApp::KclientHeight, kFullBlurVRtvIndex, fullBlurVSrvIndex);

    // 半解像度パス共通のビューポート・シザー矩形
    bloomViewport.TopLeftX = 0.0f;
    bloomViewport.TopLeftY = 0.0f;
    bloomViewport.Width = static_cast<float>(kBloomWidth);
    bloomViewport.Height = static_cast<float>(kBloomHeight);
    bloomViewport.MinDepth = 0.0f;
    bloomViewport.MaxDepth = 1.0f;

    bloomScissorRect.left = 0;
    bloomScissorRect.top = 0;
    bloomScissorRect.right = static_cast<LONG>(kBloomWidth);
    bloomScissorRect.bottom = static_cast<LONG>(kBloomHeight);

    // 5. パイプライン生成
    CreateGraphicsPipeline();
    CreateExtractPipeline();
    CreateBlurPipeline();

    // 6. 頂点リソースの作成 (画面全体を覆う板ポリ)
    vertexResource = dxCommon->CreatBufferResource(sizeof(VertexData) * 4);
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferView.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    // NDC座標系 (-1.0 ～ 1.0)
    vertexData[0] = { {-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f} }; // 左下
    vertexData[1] = { {-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} }; // 左上
    vertexData[2] = { { 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f} }; // 右下
    vertexData[3] = { { 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f} }; // 右上
    vertexResource->Unmap(0, nullptr);
}

Microsoft::WRL::ComPtr<ID3D12Resource> PostProcess::CreateBloomTexture(uint32_t width, uint32_t height, uint32_t rtvIdx, uint32_t& outSrvIndex) {
    // ブルーム／画面全体ぼかし用テクスチャは全て同一フォーマットのため手順を共通化（解像度のみ引数化）
    D3D12_RESOURCE_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.DepthOrArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = desc.Format;
    for (int i = 0; i < 4; ++i) clearValue.Color[i] = DirectXCommon::kClearColor[i];

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&texture)
    );
    assert(SUCCEEDED(hr));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dxCommon->GetDevice()->CreateRenderTargetView(texture.Get(), &rtvDesc, dxCommon->GetRtvHandle(rtvIdx));

    outSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVForTexture2D(outSrvIndex, texture.Get(), desc.Format, 1);

    return texture;
}

void PostProcess::CreateGraphicsPipeline() {
    auto device = dxCommon->GetDevice();

    // --- 1. RootSignature の作成 ---
    // t0=シーン, t1=ブルーム を別々のテーブルにする（SrvManager::Allocate()のインデックスが
    // 連続する保証がないため、1テーブルに2レンジまとめると誤ったディスクリプタを参照してしまう）
    D3D12_DESCRIPTOR_RANGE sceneRange[1] = {};
    sceneRange[0].BaseShaderRegister = 0; // t0
    sceneRange[0].NumDescriptors = 1;
    sceneRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sceneRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE bloomRange[1] = {};
    bloomRange[0].BaseShaderRegister = 1; // t1
    bloomRange[0].NumDescriptors = 1;
    bloomRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bloomRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE fullBlurRange[1] = {};
    fullBlurRange[0].BaseShaderRegister = 2; // t2
    fullBlurRange[0].NumDescriptors = 1;
    fullBlurRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    fullBlurRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = sceneRange;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = bloomRange;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = fullBlurRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Constants.ShaderRegister = 0; // b0: bloomIntensity, grayscaleフラグ, sepiaフラグ, blurEnabledフラグ, vignetteIntensity, vignetteRadius, damageVignetteIntensity
    rootParameters[3].Constants.Num32BitValues = 7;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0; // s0
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&compositeRootSignature));
    assert(SUCCEEDED(hr));

    // --- 2. シェーダーのコンパイル ---
    IDxcUtils* dxcUtils;
    IDxcCompiler3* dxcCompiler;
    IDxcIncludeHandler* includeHandler;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

    auto vsBlob = CompileShader(L"Resources/shaders/PostProcess.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler, std::cout);
    auto psBlob = CompileShader(L"Resources/shaders/PostProcess.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler, std::cout);
    assert(vsBlob && psBlob);

    // --- 3. PSO の作成 ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = compositeRootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.DepthStencilState.DepthEnable = FALSE; // 深度テストなし
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&compositePipelineState));
    assert(SUCCEEDED(hr));
}

void PostProcess::CreateExtractPipeline() {
    auto device = dxCommon->GetDevice();

    // --- 1. RootSignature の作成 ---
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0; // t0
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Constants.ShaderRegister = 0; // b0: bloomThreshold
    rootParameters[1].Constants.Num32BitValues = 1;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0; // s0
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&extractRootSignature));
    assert(SUCCEEDED(hr));

    // --- 2. シェーダーのコンパイル ---
    IDxcUtils* dxcUtils;
    IDxcCompiler3* dxcCompiler;
    IDxcIncludeHandler* includeHandler;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

    auto vsBlob = CompileShader(L"Resources/shaders/PostProcess.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler, std::cout);
    auto psBlob = CompileShader(L"Resources/shaders/BrightnessExtract.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler, std::cout);
    assert(vsBlob && psBlob);

    // --- 3. PSO の作成 ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = extractRootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&extractPipelineState));
    assert(SUCCEEDED(hr));
}

void PostProcess::CreateBlurPipeline() {
    auto device = dxCommon->GetDevice();

    // --- 1. RootSignature の作成 ---
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0; // t0
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Constants.ShaderRegister = 0; // b0: texelSize.xy, direction, blurStrength
    rootParameters[1].Constants.Num32BitValues = 4;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0; // s0
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&blurRootSignature));
    assert(SUCCEEDED(hr));

    // --- 2. シェーダーのコンパイル ---
    IDxcUtils* dxcUtils;
    IDxcCompiler3* dxcCompiler;
    IDxcIncludeHandler* includeHandler;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

    auto vsBlob = CompileShader(L"Resources/shaders/PostProcess.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler, std::cout);
    auto psBlob = CompileShader(L"Resources/shaders/GaussianBlur.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler, std::cout);
    assert(vsBlob && psBlob);

    // --- 3. PSO の作成 ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = blurRootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&blurPipelineState));
    assert(SUCCEEDED(hr));

    // ボックスフィルターPSO：ルートシグネチャ・頂点シェーダー・各種ステートはガウシアンと共用し、
    // ピクセルシェーダーだけBoxBlur.PS.hlslに差し替える
    auto boxPsBlob = CompileShader(L"Resources/shaders/BoxBlur.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler, std::cout);
    assert(boxPsBlob);
    psoDesc.PS = { boxPsBlob->GetBufferPointer(), boxPsBlob->GetBufferSize() };
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&boxBlurPipelineState));
    assert(SUCCEEDED(hr));
}

void PostProcess::PreDraw() {
    auto commandList = dxCommon->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetRtvHandle(rtvIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    commandList->ClearRenderTargetView(rtvHandle, DirectXCommon::kClearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
}

void PostProcess::PostDraw() {
    auto commandList = dxCommon->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    UINT backBufferIndex = dxCommon->GetSwapChain()->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetRtvHandle(backBufferIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
}

void PostProcess::Update(bool showDebugUI) {
    // グレースケール・フラッシュ／被弾ヴィネットのタイマーはデバッグUI表示状態に関係なく常に進める
    if (grayscaleFlashTimer > 0.0f) {
        grayscaleFlashTimer -= 1.0f / 60.0f;
        if (grayscaleFlashTimer < 0.0f) grayscaleFlashTimer = 0.0f;
    }
    if (damageVignetteTimer > 0.0f) {
        damageVignetteTimer -= 1.0f / 60.0f;
        if (damageVignetteTimer < 0.0f) damageVignetteTimer = 0.0f;
    }

    if (!showDebugUI) return;

#ifdef _DEBUG
    // Release/DevelopmentではImGuiコンテキスト自体を生成しない（Framework::Initialize参照）ため、
    // ここに到達するとBegin()がコンテキスト無しで呼ばれてしまう。Debugビルドのみに限定する
    ImGui::Begin("PostProcess");
    ImGui::DragFloat("Bloom Threshold", &bloomThreshold, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Bloom Intensity", &bloomIntensity, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Bloom Blur Strength", &blurStrength, 0.01f, 0.0f, 10.0f);

    ImGui::Separator();
    ImGui::Checkbox("Screen Blur Enabled (Gaussian)", &blurEnabled);
    ImGui::DragFloat("Screen Blur Strength (Gaussian)", &screenBlurStrength, 0.01f, 0.0f, 10.0f);
    ImGui::Checkbox("Screen Blur Enabled (Box)", &boxBlurEnabled);
    ImGui::DragFloat("Screen Blur Strength (Box)", &boxBlurStrength, 0.01f, 0.0f, 10.0f);

    ImGui::Separator();
    ImGui::Checkbox("Vignette Enabled", &vignetteEnabled);
    ImGui::DragFloat("Vignette Intensity", &vignetteIntensity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Vignette Radius", &vignetteRadius, 0.01f, 0.0f, 1.0f);

    ImGui::End();
#endif
}

void PostProcess::DrawBrightnessExtractPass() {
    auto commandList = dxCommon->GetCommandList();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = brightnessResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);

    commandList->RSSetViewports(1, &bloomViewport);
    commandList->RSSetScissorRects(1, &bloomScissorRect);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetRtvHandle(kBrightnessRtvIndex);
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    commandList->SetGraphicsRootSignature(extractRootSignature.Get());
    commandList->SetPipelineState(extractPipelineState.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

    commandList->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(srvIndex));
    commandList->SetGraphicsRoot32BitConstants(1, 1, &bloomThreshold, 0);

    commandList->DrawInstanced(4, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void PostProcess::DrawGaussianBlurPass(bool horizontal, ID3D12Resource* srcResource, uint32_t srcSrvIndex, ID3D12Resource* dstResource, uint32_t dstRtvIndex,
    D3D12_VIEWPORT viewport, D3D12_RECT scissorRect, float texelWidth, float texelHeight, float strength, bool useBoxFilter) {
    auto commandList = dxCommon->GetCommandList();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dstResource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetRtvHandle(dstRtvIndex);
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    commandList->SetGraphicsRootSignature(blurRootSignature.Get());
    commandList->SetPipelineState(useBoxFilter ? boxBlurPipelineState.Get() : blurPipelineState.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

    commandList->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(srcSrvIndex));

    // b0: texelSize.xy, direction(0=水平/1=垂直), blurStrength
    float blurParam[4] = {
        texelWidth,
        texelHeight,
        horizontal ? 0.0f : 1.0f,
        strength
    };
    commandList->SetGraphicsRoot32BitConstants(1, 4, blurParam, 0);

    commandList->DrawInstanced(4, 1, 0, 0);

    (void)srcResource; // バリア対象はdst側のみのため未使用（引数は仕様上受け取る）

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void PostProcess::DrawFullScreenBlurPass() {
    // ガウシアン・ボックスそれぞれ独立にON/OFFできる。両方ONの場合はボックスを優先する
    if (!blurEnabled && !boxBlurEnabled) return;
    bool useBox = boxBlurEnabled;
    float strength = useBox ? boxBlurStrength : screenBlurStrength;

    D3D12_VIEWPORT viewport = dxCommon->GetViewport();
    D3D12_RECT scissorRect = dxCommon->GetScissorRect();
    float texelWidth = 1.0f / static_cast<float>(WinApp::KclientWidth);
    float texelHeight = 1.0f / static_cast<float>(WinApp::KclientHeight);

    // シーンテクスチャ(resource) -> 水平ブラー(fullBlurHResource) -> 垂直ブラー(fullBlurVResource=最終結果)
    DrawGaussianBlurPass(true, resource.Get(), srvIndex, fullBlurHResource.Get(), kFullBlurHRtvIndex,
        viewport, scissorRect, texelWidth, texelHeight, strength, useBox);
    DrawGaussianBlurPass(false, fullBlurHResource.Get(), fullBlurHSrvIndex, fullBlurVResource.Get(), kFullBlurVRtvIndex,
        viewport, scissorRect, texelWidth, texelHeight, strength, useBox);
}

void PostProcess::DrawCompositePass() {
    auto commandList = dxCommon->GetCommandList();

    // 半解像度パスの後なのでフル解像度に戻す（忘れるとレティクル等のUIまで半解像度で歪む）
    D3D12_VIEWPORT viewport = dxCommon->GetViewport();
    D3D12_RECT scissorRect = dxCommon->GetScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    UINT backBufferIndex = dxCommon->GetSwapChain()->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetRtvHandle(backBufferIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    commandList->SetGraphicsRootSignature(compositeRootSignature.Get());
    commandList->SetPipelineState(compositePipelineState.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

    // resource(シーン)・blurVResource(ブルーム)・fullBlurVResource(全体ぼかし)はどれも既にPSR状態のためバリア不要
    commandList->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(srvIndex));
    commandList->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(blurVSrvIndex));
    commandList->SetGraphicsRootDescriptorTable(2, srvManager->GetGPUDescriptorHandle(fullBlurVSrvIndex));

    // グレースケール・フラッシュは三角波（前半0→1でフェードイン、後半1→0でフェードアウト）で強さを求め、
    // 手動トグル(grayscaleEnabled)とは独立させつつ、どちらか強い方を採用する
    float grayscaleFlashRatio = 0.0f;
    if (grayscaleFlashTimer > 0.0f) {
        float t = 1.0f - (grayscaleFlashTimer / kFlashDuration); // 0(開始)→1(終了)
        grayscaleFlashRatio = (t < 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f);
    }
    float grayscaleBase = grayscaleEnabled ? 1.0f : 0.0f;
    float grayscaleBlend = (grayscaleFlashRatio > grayscaleBase) ? grayscaleFlashRatio : grayscaleBase;

    // 被弾ヴィネットはグレースケール・フラッシュ（三角波）とは異なり、被弾直後に最大→時間経過で薄くなる単純な減衰
    float damageVignetteRatio = damageVignetteTimer / kImpactVignetteDuration; // 1(開始)→0(終了)
    float damageVignetteIntensity = damageVignetteRatio * kImpactVignetteMaxBlend;

    float compositeParam[7] = {
        bloomIntensity, grayscaleBlend, sepiaEnabled ? 1.0f : 0.0f, (blurEnabled || boxBlurEnabled) ? 1.0f : 0.0f,
        vignetteEnabled ? vignetteIntensity : 0.0f, vignetteRadius, damageVignetteIntensity
    };
    commandList->SetGraphicsRoot32BitConstants(3, 7, compositeParam, 0);

    commandList->DrawInstanced(4, 1, 0, 0);
}

void PostProcess::Draw() {
    DrawBrightnessExtractPass();
    DrawGaussianBlurPass(true, brightnessResource.Get(), brightnessSrvIndex, blurHResource.Get(), kBlurHRtvIndex,
        bloomViewport, bloomScissorRect, 1.0f / static_cast<float>(kBloomWidth), 1.0f / static_cast<float>(kBloomHeight), blurStrength);
    DrawGaussianBlurPass(false, blurHResource.Get(), blurHSrvIndex, blurVResource.Get(), kBlurVRtvIndex,
        bloomViewport, bloomScissorRect, 1.0f / static_cast<float>(kBloomWidth), 1.0f / static_cast<float>(kBloomHeight), blurStrength);
    DrawFullScreenBlurPass();
    DrawCompositePass();
}
