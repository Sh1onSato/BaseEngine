#include "Object3dCommon.h"
#include "ShaderCompiler.h"
#include "Logger.h"
#include <cmath>


using namespace ShaderCompiler;
using namespace Logger;

void Object3dCommon::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
	this->dxCommon = dxCommon;
	this->srvManager = srvManager;
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    assert(SUCCEEDED(hr));
	// 1. ルートシグネチャの作成
	CreateRootSignature();
	// 2. パイプラインの作成
	CreateGraphicsPipeline();

    // DirectionalLight用のリソース
    directionalLightResource = dxCommon->CreatBufferResource(sizeof(DirectionalLight));
    directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));

    directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData->direction = Calculation::Normalize(Vector3{ -0.5f, -1.0f, 0.3f }); // 斜め上からの太陽光
    directionalLightData->intensity = 1.0f;

    // PointLight用のリソース（デフォルトでは無効。呼び出し側(GetPointLightData()経由)で位置・intensityを設定して有効化する想定）
    pointLightResource = dxCommon->CreatBufferResource(sizeof(PointLight));
    pointLightResource->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData));
    pointLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    pointLightData->position = { 0.0f, 0.0f, 0.0f };
    pointLightData->intensity = 0.0f; // デフォルトは無効(0)。シーン側で用途に応じて設定する
    pointLightData->radius = 8.0f;
    pointLightData->decay = 1.0f;

    // SpotLight用のリソース（デフォルトでは無効。呼び出し側(GetSpotLightData()経由)で位置・向き・intensityを設定して有効化する想定）
    spotLightResource = dxCommon->CreatBufferResource(sizeof(SpotLight));
    spotLightResource->Map(0, nullptr, reinterpret_cast<void**>(&spotLightData));
    spotLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    spotLightData->position = { 0.0f, 0.0f, 0.0f };
    spotLightData->intensity = 0.0f; // デフォルトは無効(0)。シーン側で用途に応じて設定する
    spotLightData->direction = Calculation::Normalize(Vector3{ 0.0f, -1.0f, 0.0f });
    spotLightData->distance = 6.0f;
    spotLightData->decay = 2.0f;
    spotLightData->cosAngle = cosf(Calculation::kPi / 3.0f);       // 外側角(60度)
    spotLightData->cosFalloffStart = cosf(Calculation::kPi / 4.0f); // 内側角(45度、cosAngleより大きい値)

    // --- シャドウマップ（ディレクショナルライト用、正射影 + PCF） ---
    shadowMapResource = dxCommon->CreateShadowMapResource(kShadowMapSize, kShadowMapSize);

    // DSV作成（dsvDescriptorHeapのインデックス1。インデックス0はメインの深度バッファが使用中）
    D3D12_DEPTH_STENCIL_VIEW_DESC shadowDsvDesc{};
    shadowDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    shadowDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = DirectXCommon::GetCPUDescriptorHandle(dxCommon->GetDsvHeap(), dxCommon->GetDescriptorSizeDSV(), 1);
    dxCommon->GetDevice()->CreateDepthStencilView(shadowMapResource.Get(), &shadowDsvDesc, shadowDsvHandle);

    // SRV確保（t1でPSからサンプリングするため）
    shadowMapSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVForDepthTexture(shadowMapSrvIndex, shadowMapResource.Get());

    // ShadowData用のリソース
    shadowDataResource = dxCommon->CreatBufferResource(sizeof(ShadowData));
    shadowDataResource->Map(0, nullptr, reinterpret_cast<void**>(&shadowData));
    shadowData->lightViewProjection = Calculation::MakeIdentity4x4();
    shadowData->bias = 0.0025f;
    shadowData->orthoExtentWorld = shadowOrthoExtent;
    shadowData->orthoDepthRangeWorld = shadowFarClip - shadowNearClip;

    // シャドウ用ルートシグネチャ・パイプラインの作成
    CreateShadowRootSignature();
    CreateShadowPipeline();

    // --- スキニング未使用のObject3d用デフォルトボーンパレット（単位行列1個） ---
    // SetBonePalette()を呼ばないObject3dは常にこれをt2にバインドすることで、
    // ルートSRV未バインドによる「ゴミ変形」を防ぐ（頂点シェーダー側は必ずgSkinMatrices[0]等を参照するため）。
    defaultBonePaletteResource = dxCommon->CreatBufferResource(sizeof(Matrix4x4));
    Matrix4x4* defaultBonePaletteData = nullptr;
    HRESULT hrPalette = defaultBonePaletteResource->Map(0, nullptr, reinterpret_cast<void**>(&defaultBonePaletteData));
    if (FAILED(hrPalette)) {
        Log("Object3dCommon: defaultBonePaletteResource Map failed.\n");
        assert(false);
    }
    *defaultBonePaletteData = Calculation::MakeIdentity4x4();
    defaultBonePaletteResource->Unmap(0, nullptr);
}

void Object3dCommon::CreateRootSignature() {
    D3D12_ROOT_PARAMETER rootParameters[9] = {};

    // CBV: Material (b0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // CBV: TransformationMatrix (b1)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 1;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // DescriptorTable: Texture (t0)
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // CBV: DirectionalLight (b2)
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].Descriptor.ShaderRegister = 2;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // CBV: PointLight (b3)
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].Descriptor.ShaderRegister = 3;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // CBV: SpotLight (b4)
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[5].Descriptor.ShaderRegister = 4;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // CBV: ShadowData (b5)
    rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[6].Descriptor.ShaderRegister = 5;
    rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // DescriptorTable: ShadowMap (t1)
    D3D12_DESCRIPTOR_RANGE shadowDescriptorRange[1] = {};
    shadowDescriptorRange[0].BaseShaderRegister = 1;
    shadowDescriptorRange[0].NumDescriptors = 1;
    shadowDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[7].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[7].DescriptorTable.pDescriptorRanges = shadowDescriptorRange;
    rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // SRV: スキニング用ボーン行列パレット (t2、StructuredBuffer<float4x4>)
    // ParticleCommon::CreateRootSignature()のt0直接バインドと同じ「ディスクリプタヒープ経由不要」の方式
    rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[8].Descriptor.ShaderRegister = 2; // t2
    rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // 静的サンプラーの設定（テクスチャ用）
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 静的サンプラーの設定（シャドウマップ用の比較サンプラー、s1）
    staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズして作成
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    hr = dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    if (FAILED(hr)) {
        Log("Object3dCommon: CreateRootSignature (main) failed.\n");
        assert(false);
    }
    Log(std::format("Object3dCommon: main root signature created. NumParameters={}\n", descriptionRootSignature.NumParameters));
}

void Object3dCommon::CreateGraphicsPipeline() {
    // --- 1. Shaderのコンパイル ---
    vertexShaderBlob = CompileShader(
        L"resources/shaders/Object3d.VS.hlsl",
        L"vs_6_0",
        dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    assert(vertexShaderBlob != nullptr);

    pixelShaderBlob = CompileShader(
        L"resources/shaders/Object3d.PS.hlsl",
        L"ps_6_0",
        dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    assert(pixelShaderBlob != nullptr);

    // --- 2. PSOの設定 ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();

    // コンパイルしたシェーダをセット
    psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };

    // InputLayout設定（3D頂点用：POSITION, TEXCOORD, NORMAL, BLENDWEIGHT0, BLENDINDICES0）
    // VertexDataのフィールド順(position,texcoord,normal,boneWeights,boneIndices)と一致させること
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[5] = {};
    inputElementDescs[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[2] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[3] = { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[4] = { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    psoDesc.InputLayout.pInputElementDescs = inputElementDescs;
    psoDesc.InputLayout.NumElements = _countof(inputElementDescs);

    // 3D用の重要な設定
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable = FALSE; // 明示的に無効化

    // ラスタライザ設定
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE; // 時計回りを表面にする

    // ブレンド設定
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // --- 3. パイプラインステート生成 (有効化) ---
    HRESULT hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    assert(SUCCEEDED(hr));

    // --- 4. 加算合成用パイプラインステート生成（発光表現用） ---
    // ParticleCommonの加算ブレンド設定と同じ式（Src*alpha + Dest*1）を使う
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    // 加算合成は重ね書きが前提のため、深度書き込みは行わない（パーティクルと同じ考え方）
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateAdditive));
    assert(SUCCEEDED(hr));
}

void Object3dCommon::CreateShadowRootSignature() {
    // b0（WVPのみ）・t2（スキニング用ボーン行列パレット）・VSのみの最小ルートシグネチャ
    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[1].Descriptor.ShaderRegister = 2; // t2
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    hr = dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignatureShadow));
    if (FAILED(hr)) {
        Log("Object3dCommon: CreateRootSignature (shadow) failed.\n");
        assert(false);
    }
}

void Object3dCommon::CreateShadowPipeline() {
    shadowVertexShaderBlob = CompileShader(
        L"resources/shaders/Shadow.VS.hlsl",
        L"vs_6_0",
        dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    assert(shadowVertexShaderBlob != nullptr);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignatureShadow.Get();
    psoDesc.VS = { shadowVertexShaderBlob->GetBufferPointer(), shadowVertexShaderBlob->GetBufferSize() };
    // PSは使用しない（深度のみ書き込む）

    // InputLayoutはメインと同じ頂点バッファを使い回すため同一（POSITIONとスキニング属性のみ使用）
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[5] = {};
    inputElementDescs[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[2] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[3] = { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[4] = { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    psoDesc.InputLayout.pInputElementDescs = inputElementDescs;
    psoDesc.InputLayout.NumElements = _countof(inputElementDescs);

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    // ラスタライザ設定（メインと同じ）
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.NumRenderTargets = 0; // カラーバッファには書き込まない
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    HRESULT hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateShadow));
    assert(SUCCEEDED(hr));
}

void Object3dCommon::PreDraw(Object3dBlendMode blendMode) {
    auto commandList = dxCommon->GetCommandList();

    // 4. パイプライン設定 ---
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->SetPipelineState(blendMode == Object3dBlendMode::kAdditive ? pipelineStateAdditive.Get() : pipelineState.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvManager->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);

    // 定数バッファのセット（DirectionalLight）
    commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(4, pointLightResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(5, spotLightResource->GetGPUVirtualAddress());

    // ライト視点のビュープロジェクション行列はPreDrawShadow()内のUpdateLightViewProjection()で
    // 計算済み（シャドウパスと同じ値をここでも使うことで1フレームのズレを防ぐ）

    // 定数バッファのセット（ShadowData / ShadowMap）
    commandList->SetGraphicsRootConstantBufferView(6, shadowDataResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(7, srvManager->GetGPUDescriptorHandle(shadowMapSrvIndex));
}

void Object3dCommon::UpdateLightViewProjection() {
    // ディレクショナルライトの向きから、ライト視点の正射影ビュープロジェクション行列を再計算する
    // （ImGuiでの向き変更にリアルタイム追従させるため）
    // 正射影範囲(shadowLightDistance/shadowOrthoExtent/shadowNearClip/shadowFarClip)は
    // シーンのスケール（マップの広さ・高さ）に依存する値。デフォルトは従来のマジックナンバーと同値。
    // 外部から変更する場合はSetShadowOrthoRange()を呼ぶこと（PS側のkOrthoExtentWorld等は
    // 定数バッファ経由で自動的に同期されるため、シェーダーの手動修正は不要）。
    Vector3 lightDirection = Calculation::Normalize(directionalLightData->direction);
    Vector3 lightPosition = Vector3{ 0.0f, 0.0f, 0.0f } - lightDirection * shadowLightDistance;
    Vector3 up = (std::abs(lightDirection.y) > 0.99f) ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };

    Matrix4x4 lightViewMatrix = Calculation::MakeLookAtMatrix(lightPosition, Vector3{ 0.0f, 0.0f, 0.0f }, up);
    Matrix4x4 lightProjectionMatrix = Calculation::MakeOrthographicMatrix(-shadowOrthoExtent, shadowOrthoExtent, shadowOrthoExtent, -shadowOrthoExtent, shadowNearClip, shadowFarClip);
    shadowData->lightViewProjection = lightViewMatrix * lightProjectionMatrix;

    // PS側のPCSS計算がワールド距離⇔UV距離の換算に使う値を定数バッファ経由で渡す
    shadowData->orthoExtentWorld = shadowOrthoExtent;
    shadowData->orthoDepthRangeWorld = shadowFarClip - shadowNearClip;
}

void Object3dCommon::SetShadowOrthoRange(float lightDistance, float orthoExtent, float nearClip, float farClip) {
    shadowLightDistance = lightDistance;
    shadowOrthoExtent = orthoExtent;
    shadowNearClip = nearClip;
    shadowFarClip = farClip;
}

void Object3dCommon::PreDrawShadow() {
    UpdateLightViewProjection();

    auto commandList = dxCommon->GetCommandList();

    // バリア: PIXEL_SHADER_RESOURCE → DEPTH_WRITE
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = shadowMapResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    commandList->ResourceBarrier(1, &barrier);

    // シャドウマップ解像度のビューポート・シザーに切り替え
    D3D12_VIEWPORT shadowViewport{};
    shadowViewport.Width = static_cast<float>(kShadowMapSize);
    shadowViewport.Height = static_cast<float>(kShadowMapSize);
    shadowViewport.TopLeftX = 0.0f;
    shadowViewport.TopLeftY = 0.0f;
    shadowViewport.MinDepth = 0.0f;
    shadowViewport.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &shadowViewport);

    D3D12_RECT shadowScissorRect{};
    shadowScissorRect.left = 0;
    shadowScissorRect.top = 0;
    shadowScissorRect.right = static_cast<LONG>(kShadowMapSize);
    shadowScissorRect.bottom = static_cast<LONG>(kShadowMapSize);
    commandList->RSSetScissorRects(1, &shadowScissorRect);

    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = DirectXCommon::GetCPUDescriptorHandle(dxCommon->GetDsvHeap(), dxCommon->GetDescriptorSizeDSV(), 1);
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsvHandle);
    commandList->ClearDepthStencilView(shadowDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList->SetGraphicsRootSignature(rootSignatureShadow.Get());
    commandList->SetPipelineState(pipelineStateShadow.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::PostDrawShadow() {
    auto commandList = dxCommon->GetCommandList();

    // バリア: DEPTH_WRITE → PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = shadowMapResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    // メインのビューポート・シザーに戻す
    commandList->RSSetViewports(1, &dxCommon->GetViewport());
    commandList->RSSetScissorRects(1, &dxCommon->GetScissorRect());
}
