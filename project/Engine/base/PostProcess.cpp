#include "PostProcess.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Logger.h"
#include "Screen.h"
#include "ShaderCompiler.h"
#include <algorithm>
#include <format>
#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

namespace {
    // 3D の描画先と同じ色の形式（RenderScaler・Object3d などの PSO と同じ）
    constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // ブルームは2回ぼかして広くにじませる
    constexpr int kBloomBlurIterations = 2;
    // SSAO: 平らな面が自分で暗くならないための差（ワールドの長さ）と、ノイズを消すぼかしの幅（画面のピクセル）
    constexpr float kAmbientOcclusionBias = 0.03f;
    constexpr float kAmbientOcclusionBlurRadius = 2.0f;

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

bool PostEffectSettings::IsActive() const {
    return (bloomEnabled && bloomIntensity > 0.0f) || blur > 0.0f || (ambientOcclusionEnabled && ambientOcclusionIntensity > 0.0f) ||
           brightness != 0.0f || contrast != 1.0f || saturation != 1.0f || sepia > 0.0f ||
           tint.x != 1.0f || tint.y != 1.0f || tint.z != 1.0f || vignetteIntensity > 0.0f;
}

void PostProcess::Initialize(DirectXCommon* newDxCommon, SrvManager* newSrvManager) {
    dxCommon = newDxCommon;
    srvManager = newSrvManager;

    for (int i = 0; i < kTargetCount; ++i) {
        // SSAO の絵は RenderScaler の番号（8）の後ろに置く
        targets[i].rtvIndex = i < kAo ? kFirstRtvIndex + static_cast<uint32_t>(i) : kFirstAoRtvIndex + static_cast<uint32_t>(i - kAo);
        targets[i].srvIndex = srvManager->Allocate();
    }

    CreateRootSignature();
    extractPipeline = CreatePipeline(L"Resources/shaders/BrightnessExtract.PS.hlsl");
    blurPipeline = CreatePipeline(L"Resources/shaders/GaussianBlur.PS.hlsl");
    compositePipeline = CreatePipeline(L"Resources/shaders/PostProcess.PS.hlsl");
    ambientOcclusionPipeline = CreatePipeline(L"Resources/shaders/SSAO.PS.hlsl");
}

void PostProcess::SetDefaultSettings(const PostEffectSettings& newDefault) {
    defaultSettings = newDefault;
    settings = newDefault;
}

void PostProcess::CreateRootSignature() {
    constexpr uint32_t kTextureCount = 3; // t0, t1, t2
    D3D12_DESCRIPTOR_RANGE ranges[kTextureCount] = {};
    for (uint32_t i = 0; i < kTextureCount; ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    // 絵の SRV の番号が続いているとは限らないので、別々のテーブルにする
    D3D12_ROOT_PARAMETER parameters[kTextureCount + 1] = {};
    for (uint32_t i = 0; i < kTextureCount; ++i) {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
        parameters[i].DescriptorTable.NumDescriptorRanges = 1;
    }
    parameters[kRootIndexConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[kRootIndexConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[kRootIndexConstants].Constants.ShaderRegister = 0;
    parameters[kRootIndexConstants].Constants.Num32BitValues = kRootConstantCount;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    const D3D12_FILTER filters[2] = { D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_POINT };
    for (uint32_t i = 0; i < 2; ++i) {
        samplers[i].Filter = filters[i];
        samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i; // s0 なめらか、s1 くっきり
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
    Logger::CheckHResult(hr, "PostProcess: D3D12SerializeRootSignature");
    hr = dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                                    IID_PPV_ARGS(&rootSignature));
    Logger::CheckHResult(hr, "PostProcess: CreateRootSignature");
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PostProcess::CreatePipeline(const wchar_t* pixelShaderPath) {
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
    Logger::CheckHResult(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)), "PostProcess: DxcCreateInstance(DxcUtils)");
    Logger::CheckHResult(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)), "PostProcess: DxcCreateInstance(DxcCompiler)");
    Logger::CheckHResult(dxcUtils->CreateDefaultIncludeHandler(&includeHandler), "PostProcess: CreateDefaultIncludeHandler");
    // 頂点は RenderScaler と同じく、画面を覆う三角形1枚を頂点番号から作る
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShader = ShaderCompiler::CompileShader(
        L"Resources/shaders/RenderScale.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShader = ShaderCompiler::CompileShader(
        pixelShaderPath, L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    if (!vertexShader || !pixelShader) {
        Logger::Fatal("PostProcess: シェーダーのコンパイルに失敗しました");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature.Get();
    desc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    desc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kColorFormat;
    // 最後の合成はバックバッファと深度が一緒に設定されているので形式だけ合わせる（深度は使わない）
    desc.DSVFormat = kDepthFormat;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    HRESULT hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
    Logger::CheckHResult(hr, "PostProcess: CreateGraphicsPipelineState");
    return pipeline;
}

void PostProcess::EnsureTargets(uint32_t width, uint32_t height) {
    if (targets[0].resource && width == sourceWidth && height == sourceHeight) {
        return;
    }
    // ブルームは軽くするため半分の解像度で行う（ぼかすので細かさは要らない）
    const uint32_t halfWidth = (std::max)(1u, width / 2);
    const uint32_t halfHeight = (std::max)(1u, height / 2);
    CreateTarget(targets[kBright], halfWidth, halfHeight);
    CreateTarget(targets[kBloomPing], halfWidth, halfHeight);
    CreateTarget(targets[kBloomPong], halfWidth, halfHeight);
    CreateTarget(targets[kBlurPing], width, height);
    CreateTarget(targets[kBlurPong], width, height);
    // SSAO もぼかすので半分の解像度でよい
    CreateTarget(targets[kAo], halfWidth, halfHeight);
    CreateTarget(targets[kAoPing], halfWidth, halfHeight);
    CreateTarget(targets[kAoPong], halfWidth, halfHeight);
    sourceWidth = width;
    sourceHeight = height;
}

void PostProcess::CreateTarget(Target& target, uint32_t width, uint32_t height) {
    // 前のフレームの描画は PostDraw で GPU の完了を待っているので、古い絵はすぐ捨ててよい
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kColorFormat;
    // 普段は「絵として読む」状態にしておき、描くときだけ描画先の状態にする
    HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&target.resource));
    Logger::CheckHResult(hr, std::format("PostProcess: CreateCommittedResource({}x{})", width, height));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = kColorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dxCommon->GetDevice()->CreateRenderTargetView(target.resource.Get(), &rtvDesc, dxCommon->GetRtvHandle(target.rtvIndex));
    srvManager->CreateSRVForTexture2D(target.srvIndex, target.resource.Get(), kColorFormat, 1);
    target.width = width;
    target.height = height;
}

void PostProcess::DrawPass(ID3D12PipelineState* pipeline, uint32_t sourceSrvIndex, Target& target,
                           const float* constants, uint32_t constantCount) {
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    Transition(commandList, target.resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon->GetRtvHandle(target.rtvIndex);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f };
    const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    commandList->SetPipelineState(pipeline);
    commandList->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(sourceSrvIndex));
    // t1・t2 は合成の段しか使わないが、テーブルを空にはできないので同じ絵を入れておく
    commandList->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(sourceSrvIndex));
    commandList->SetGraphicsRootDescriptorTable(2, srvManager->GetGPUDescriptorHandle(sourceSrvIndex));
    commandList->SetGraphicsRoot32BitConstants(kRootIndexConstants, constantCount, constants, 0);
    commandList->DrawInstanced(3, 1, 0, 0);

    Transition(commandList, target.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void PostProcess::Blur(uint32_t sourceSrvIndex, Target& ping, Target& result, float radius) {
    // GaussianBlur.PS.hlsl の b0: 1 回にずらす量（UV）。横 → 縦の2回に分けるとずっと軽い。
    // 幅は「画面（黒帯の内側）の 1 ピクセル × radius」にする（内部解像度を下げても、ぼけ方が画面上で同じになるように）
    const D3D12_VIEWPORT& screenViewport = Screen::GetViewport();
    const float horizontal[2] = { radius / (std::max)(screenViewport.Width, 1.0f), 0.0f };
    const float vertical[2] = { 0.0f, radius / (std::max)(screenViewport.Height, 1.0f) };
    DrawPass(blurPipeline.Get(), sourceSrvIndex, ping, horizontal, 2);
    DrawPass(blurPipeline.Get(), ping.srvIndex, result, vertical, 2);
}

void PostProcess::Render(uint32_t sourceSrvIndex, uint32_t width, uint32_t height, bool usePointFilter, uint32_t depthSrvIndex) {
    EnsureTargets(width, height);
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12DescriptorHeap* heaps[] = { srvManager->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // 1. ブルーム: 明るい所だけ取り出して（半分の解像度に縮めながら）、ぼかす
    const bool useBloom = settings.bloomEnabled && settings.bloomIntensity > 0.0f;
    if (useBloom) {
        const float threshold[1] = { settings.bloomThreshold };
        DrawPass(extractPipeline.Get(), sourceSrvIndex, targets[kBright], threshold, 1);
        uint32_t bloomSource = targets[kBright].srvIndex;
        for (int i = 0; i < kBloomBlurIterations; ++i) {
            // 半分の解像度でぼかすので、等倍のときと同じ広がりになるよう幅を倍にする
            Blur(bloomSource, targets[kBloomPing], targets[kBloomPong], settings.bloomRadius * 2.0f);
            bloomSource = targets[kBloomPong].srvIndex;
        }
    }

    // 2. 接している所の暗さ（SSAO）: 深度から求め（半分の解像度）、ノイズが消えるくらいぼかす
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    const bool useAmbientOcclusion = UsesDepth() && depthSrvIndex != kNoDepth && camera;
    if (useAmbientOcclusion) {
        // SSAO.PS.hlsl の AoParams と同じ並び（深度から位置に戻すのに、描いたカメラの投影行列の成分を使う）
        const Matrix4x4& projection = camera->GetProjectionMatrix();
        const float aoConstants[10] = {
            projection.m[0][0], projection.m[1][1], projection.m[2][2], projection.m[3][2],
            settings.ambientOcclusionRadius, settings.ambientOcclusionIntensity, kAmbientOcclusionBias, 0.0f,
            1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height),
        };
        DrawPass(ambientOcclusionPipeline.Get(), depthSrvIndex, targets[kAo], aoConstants, 10);
        Blur(targets[kAo].srvIndex, targets[kAoPing], targets[kAoPong], kAmbientOcclusionBlurRadius);
    }

    // 3. 画面全体のぼかし（等倍）
    uint32_t sceneSrvIndex = sourceSrvIndex;
    if (settings.blur > 0.0f) {
        Blur(sourceSrvIndex, targets[kBlurPing], targets[kBlurPong], settings.blur);
        sceneSrvIndex = targets[kBlurPong].srvIndex;
    }

    // 4. 合成: 画面（黒帯の内側）に、SSAO を掛け・ブルームを足し・色を整え・ヴィネットを掛けて写す
    dxCommon->BindMainRenderTarget();
    const PostEffectSettings& s = settings;
    // PostProcess.PS.hlsl の CompositeParams と同じ並び（float3 は 16 バイトの境目をまたがないように置く）
    const float constants[kRootConstantCount] = {
        useBloom ? s.bloomIntensity : 0.0f, s.brightness, s.contrast, s.saturation,
        s.sepia, s.vignetteIntensity, s.vignetteRadius, usePointFilter ? 1.0f : 0.0f,
        s.tint.x, s.tint.y, s.tint.z, useAmbientOcclusion ? 1.0f : 0.0f,
        s.vignetteColor.x, s.vignetteColor.y, s.vignetteColor.z, static_cast<float>(width) / static_cast<float>(height),
    };
    commandList->SetPipelineState(compositePipeline.Get());
    commandList->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(sceneSrvIndex));
    commandList->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(targets[kBloomPong].srvIndex));
    commandList->SetGraphicsRootDescriptorTable(2, srvManager->GetGPUDescriptorHandle(targets[kAoPong].srvIndex));
    commandList->SetGraphicsRoot32BitConstants(kRootIndexConstants, kRootConstantCount, constants, 0);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void PostProcess::DrawDebugUI() {
#ifdef _DEBUG
    PostEffectSettings& s = settings;
    ImGui::Begin("PostProcess [F6]");
    // ImGui の文字は英数字のみ（日本語のフォントを読んでいない）
    ImGui::Text(s.IsActive() ? "Active" : "Inactive (no effect)");
    ImGui::Checkbox("Bloom", &s.bloomEnabled);
    ImGui::SliderFloat("Bloom Threshold", &s.bloomThreshold, 0.0f, 1.0f);
    ImGui::SliderFloat("Bloom Intensity", &s.bloomIntensity, 0.0f, 3.0f);
    ImGui::SliderFloat("Bloom Radius", &s.bloomRadius, 0.0f, 5.0f);
    ImGui::Separator();
    ImGui::Checkbox("Ambient Occlusion", &s.ambientOcclusionEnabled);
    ImGui::SliderFloat("AO Radius", &s.ambientOcclusionRadius, 0.05f, 3.0f);
    ImGui::SliderFloat("AO Intensity", &s.ambientOcclusionIntensity, 0.0f, 2.0f);
    ImGui::Separator();
    ImGui::SliderFloat("Blur", &s.blur, 0.0f, 5.0f);
    ImGui::Separator();
    ImGui::SliderFloat("Brightness", &s.brightness, -1.0f, 1.0f);
    ImGui::SliderFloat("Contrast", &s.contrast, 0.0f, 2.0f);
    ImGui::SliderFloat("Saturation", &s.saturation, 0.0f, 2.0f);
    ImGui::SliderFloat("Sepia", &s.sepia, 0.0f, 1.0f);
    ImGui::ColorEdit3("Tint", &s.tint.x);
    ImGui::Separator();
    ImGui::SliderFloat("Vignette Intensity", &s.vignetteIntensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Vignette Radius", &s.vignetteRadius, 0.0f, 1.0f);
    ImGui::ColorEdit3("Vignette Color", &s.vignetteColor.x);
    if (ImGui::Button("Reset (default)")) {
        ResetToDefault();
    }
    ImGui::End();
#endif
}
