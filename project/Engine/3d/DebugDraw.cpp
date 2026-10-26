#include "DebugDraw.h"
#include "Calculation.h"
#include "Camera.h"
#include "Camera2D.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameTime.h"
#include "Logger.h"
#include "Screen.h"
#include "ShaderCompiler.h"
#include "SpriteCommon.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <type_traits>

using namespace ShaderCompiler;

DebugDraw* DebugDraw::instance = nullptr;

namespace {
    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;
    // 物の面とぴったり重なる線（箱のコライダーの辺など）が面に負けてちらつかないよう、少しだけ手前に出す（深度の目盛りの数）
    constexpr INT kDepthBias = -100;
    constexpr float kArrowHeadRatio = 0.2f;  // 矢じりの長さ（線の長さに対する割合）
    constexpr float kArrowHeadWidth = 0.4f;  // 矢じりの開き（矢じりの長さに対する割合）
    constexpr float kMinLength = 1.0e-6f;

    // normal に垂直な2本の軸（長さ1、互いに垂直）
    void MakePerpendicularAxes(const Vector3& normal, Vector3& outU, Vector3& outV) {
        const Vector3 n = Calculation::Normalize(normal);
        // normal とほぼ平行な軸を選ぶと外積が 0 になるので、y 軸に近いときは x 軸を使う
        const Vector3 helper = std::fabs(n.y) < 0.99f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
        outU = Calculation::Normalize(Calculation::Cross(helper, n));
        outV = Calculation::Cross(n, outU);
    }

    // 中心 center、軸 u・v の面で、角度 startAngle から endAngle までの円弧
    void AddArc(const Vector3& center, const Vector3& u, const Vector3& v, float radius, float startAngle, float endAngle,
                int segments, const Vector4& color, float duration, bool depthTest) {
        Vector3 previous = center + (u * std::cos(startAngle) + v * std::sin(startAngle)) * radius;
        for (int i = 1; i <= segments; ++i) {
            const float angle = startAngle + (endAngle - startAngle) * static_cast<float>(i) / static_cast<float>(segments);
            const Vector3 current = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            DebugDraw::Line(previous, current, color, duration, depthTest);
            previous = current;
        }
    }
}

DebugDraw::DebugDraw() = default;

DebugDraw::~DebugDraw() {
    if (instance == this) {
        instance = nullptr;
    }
}

void DebugDraw::Initialize(DirectXCommon* newDxCommon) {
    dxCommon = newDxCommon;
    instance = this;
#ifdef _DEBUG
    isEnabled = true;
#endif

    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    Logger::CheckHResult(hr, "DebugDraw: DxcCreateInstance(DxcUtils)");
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    Logger::CheckHResult(hr, "DebugDraw: DxcCreateInstance(DxcCompiler)");
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    Logger::CheckHResult(hr, "DebugDraw: CreateDefaultIncludeHandler");

    CreateRootSignature();
    CreatePipelineState(true, depthTestPipeline);
    CreatePipelineState(false, onTopPipeline);

    for (VertexBuffer& buffer : buffers3D) {
        EnsureCapacity(buffer, kInitialVertexCapacity);
    }
    EnsureCapacity(buffer2DWorld, kInitialVertexCapacity);
    EnsureCapacity(buffer2DScreen, kInitialVertexCapacity);
}

void DebugDraw::CreateRootSignature() {
    // ビュー × プロジェクション行列 (b0) をルート定数で渡す（線の種類ごとに行列が違うだけなので定数バッファを作らない）
    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].Constants.ShaderRegister = 0;
    rootParameters[0].Constants.Num32BitValues = sizeof(Matrix4x4) / sizeof(uint32_t);
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC description = {};
    description.pParameters = rootParameters;
    description.NumParameters = _countof(rootParameters);
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr) && errorBlob) {
        Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
    }
    Logger::CheckHResult(hr, "DebugDraw: D3D12SerializeRootSignature");

    hr = dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                                    IID_PPV_ARGS(&rootSignature));
    Logger::CheckHResult(hr, "DebugDraw: CreateRootSignature");
}

void DebugDraw::CreatePipelineState(bool depthTest, Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPipeline) {
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
        L"Resources/shaders/DebugLine.VS.hlsl", L"vs_6_0",
        dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
        L"Resources/shaders/DebugLine.PS.hlsl", L"ps_6_0",
        dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
    if (!vertexShaderBlob || !pixelShaderBlob) {
        Logger::Fatal("DebugDraw: DebugLine のシェーダーのコンパイルに失敗しました");
    }

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
    inputElementDescs[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[1] = { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature.Get();
    desc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    desc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
    desc.InputLayout.pInputElementDescs = inputElementDescs;
    desc.InputLayout.NumElements = _countof(inputElementDescs);

    // 半透明の色も使えるように通常のアルファブレンド
    D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.DepthClipEnable = TRUE; // カメラの後ろに回り込んだ線を切る
    desc.RasterizerState.AntialiasedLineEnable = TRUE;
    desc.RasterizerState.DepthBias = depthTest ? kDepthBias : 0;

    // 線は深度を書かない（後から描く物を隠さない）。隠れない線は深度を見ない
    desc.DepthStencilState.DepthEnable = depthTest ? TRUE : FALSE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = depthTest ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    HRESULT hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&outPipeline));
    Logger::CheckHResult(hr, "DebugDraw: CreateGraphicsPipelineState");
}

void DebugDraw::EnsureCapacity(VertexBuffer& buffer, size_t vertexCount) {
    if (buffer.resource && vertexCount <= buffer.capacity) {
        return;
    }
    // 足りなくなったら倍に広げる。前のフレームの描画は PostDraw で GPU の完了を待っているので、古いバッファはすぐ捨ててよい
    // （1つのバッファは1フレームに1回しか描かないので、このフレームの描画命令が古いバッファを指していることもない）
    const size_t newCapacity = (std::max)(vertexCount, buffer.capacity * 2);
    buffer.resource = dxCommon->CreatBufferResource(sizeof(Vertex) * newCapacity);
    Logger::CheckHResult(buffer.resource->Map(0, nullptr, reinterpret_cast<void**>(&buffer.data)), "DebugDraw: Map vertexBuffer");
    buffer.capacity = newCapacity;
}

// ---------------------------------------------------------------------------
// 全体の設定
// ---------------------------------------------------------------------------

void DebugDraw::SetEnabled(bool enabled) {
    if (!instance) {
        return;
    }
    instance->isEnabled = enabled;
    if (!enabled) {
        Clear();
    }
}

bool DebugDraw::IsEnabled() {
    return instance && instance->isEnabled;
}

void DebugDraw::SetCamera(Camera* newCamera) {
    if (instance) {
        instance->camera = newCamera;
    }
}

void DebugDraw::SetColliderVisible(bool visible) {
    if (instance) {
        instance->isColliderVisible = visible;
    }
}

bool DebugDraw::IsColliderVisible() {
    return IsEnabled() && instance->isColliderVisible;
}

void DebugDraw::Clear() {
    if (!instance) {
        return;
    }
    for (std::vector<Line3DEntry>& lines : instance->lines3D) {
        lines.clear();
    }
    instance->lines2DWorld.clear();
    instance->lines2DScreen.clear();
}

size_t DebugDraw::GetLineCount() {
    if (!instance) {
        return 0;
    }
    size_t count = instance->lines2DWorld.size() + instance->lines2DScreen.size();
    for (const std::vector<Line3DEntry>& lines : instance->lines3D) {
        count += lines.size();
    }
    return count;
}

bool DebugDraw::HasRoom(size_t currentCount) {
    if (currentCount < kMaxLinesPerList) {
        return true;
    }
    if (!hasWarnedLimit) {
        Logger::Log(std::format("DebugDraw: 線が {} 本を超えたので、それ以上は描きません（duration の長い線を毎フレーム足していないか確認）\n",
                                kMaxLinesPerList));
        hasWarnedLimit = true;
    }
    return false;
}

void DebugDraw::AddLine3D(const Vector3& start, const Vector3& end, const Vector4& color, float duration, bool depthTest) {
    std::vector<Line3DEntry>& lines = lines3D[static_cast<size_t>(depthTest ? Pass::kDepthTest : Pass::kOnTop)];
    if (HasRoom(lines.size())) {
        lines.push_back({ start, end, color, duration });
    }
}

void DebugDraw::AddLine2D(const Vector2& start, const Vector2& end, const Vector4& color, float duration, bool useCamera) {
    std::vector<Line2DEntry>& lines = useCamera ? lines2DWorld : lines2DScreen;
    if (HasRoom(lines.size())) {
        lines.push_back({ start, end, color, duration });
    }
}

// ---------------------------------------------------------------------------
// 3D
// ---------------------------------------------------------------------------

void DebugDraw::Line(const Vector3& start, const Vector3& end, const Vector4& color, float duration, bool depthTest) {
    if (IsEnabled()) {
        instance->AddLine3D(start, end, color, duration, depthTest);
    }
}

void DebugDraw::Ray(const ::Ray& ray, float length, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled() || Calculation::Length(ray.direction) < kMinLength) {
        return;
    }
    Line(ray.origin, ray.origin + Calculation::Normalize(ray.direction) * length, color, duration, depthTest);
}

void DebugDraw::Arrow(const Vector3& start, const Vector3& end, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled()) {
        return;
    }
    Line(start, end, color, duration, depthTest);
    const Vector3 direction = end - start;
    const float length = Calculation::Length(direction);
    if (length < kMinLength) {
        return;
    }
    // 先端から少し戻った所に、4方向へ開いた矢じり
    const Vector3 forward = direction * (1.0f / length);
    Vector3 u;
    Vector3 v;
    MakePerpendicularAxes(forward, u, v);
    const float headLength = length * kArrowHeadRatio;
    const Vector3 base = end - forward * headLength;
    const float headWidth = headLength * kArrowHeadWidth;
    Line(end, base + u * headWidth, color, duration, depthTest);
    Line(end, base - u * headWidth, color, duration, depthTest);
    Line(end, base + v * headWidth, color, duration, depthTest);
    Line(end, base - v * headWidth, color, duration, depthTest);
}

void DebugDraw::Cross(const Vector3& center, float size, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled()) {
        return;
    }
    const float half = size * 0.5f;
    Line(center - Vector3{ half, 0.0f, 0.0f }, center + Vector3{ half, 0.0f, 0.0f }, color, duration, depthTest);
    Line(center - Vector3{ 0.0f, half, 0.0f }, center + Vector3{ 0.0f, half, 0.0f }, color, duration, depthTest);
    Line(center - Vector3{ 0.0f, 0.0f, half }, center + Vector3{ 0.0f, 0.0f, half }, color, duration, depthTest);
}

void DebugDraw::Circle(const Vector3& center, const Vector3& normal, float radius, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled() || Calculation::Length(normal) < kMinLength) {
        return;
    }
    Vector3 u;
    Vector3 v;
    MakePerpendicularAxes(normal, u, v);
    AddArc(center, u, v, radius, 0.0f, kTwoPi, kCircleSegments, color, duration, depthTest);
}

void DebugDraw::Sphere(const ::Sphere& sphere, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled()) {
        return;
    }
    Circle(sphere.center, { 1.0f, 0.0f, 0.0f }, sphere.radius, color, duration, depthTest);
    Circle(sphere.center, { 0.0f, 1.0f, 0.0f }, sphere.radius, color, duration, depthTest);
    Circle(sphere.center, { 0.0f, 0.0f, 1.0f }, sphere.radius, color, duration, depthTest);
}

void DebugDraw::Box(const AABB& box, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled()) {
        return;
    }
    const Vector3 center = (box.min + box.max) * 0.5f;
    const Vector3 halfSize = (box.max - box.min) * 0.5f;
    OBB obb{};
    obb.center = center;
    obb.axes[0] = { 1.0f, 0.0f, 0.0f };
    obb.axes[1] = { 0.0f, 1.0f, 0.0f };
    obb.axes[2] = { 0.0f, 0.0f, 1.0f };
    obb.halfSize = halfSize;
    Box(obb, color, duration, depthTest);
}

void DebugDraw::Box(const OBB& box, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled()) {
        return;
    }
    const Vector3 x = box.axes[0] * box.halfSize.x;
    const Vector3 y = box.axes[1] * box.halfSize.y;
    const Vector3 z = box.axes[2] * box.halfSize.z;
    // 8つの角。番号のビット 0/1/2 が x/y/z の +側か−側か
    Vector3 corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = box.center + ((i & 1) ? x : x * -1.0f) + ((i & 2) ? y : y * -1.0f) + ((i & 4) ? z : z * -1.0f);
    }
    // ビットが1つだけ違う角どうしを結ぶと12本の辺になる
    for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit < 8; bit <<= 1) {
            if ((i & bit) == 0) {
                Line(corners[i], corners[i | bit], color, duration, depthTest);
            }
        }
    }
}

void DebugDraw::Capsule(const ::Capsule& capsule, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled()) {
        return;
    }
    const Vector3& start = capsule.segment.start;
    const Vector3& end = capsule.segment.end;
    const float radius = capsule.radius;
    const Vector3 axis = end - start;
    const float length = Calculation::Length(axis);
    if (length < kMinLength) {
        Sphere({ start, radius }, color, duration, depthTest);
        return;
    }
    const Vector3 forward = axis * (1.0f / length);
    Vector3 u;
    Vector3 v;
    MakePerpendicularAxes(forward, u, v);
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr int kHalfSegments = kCircleSegments / 2;
    // 両端の輪
    AddArc(start, u, v, radius, 0.0f, kTwoPi, kCircleSegments, color, duration, depthTest);
    AddArc(end, u, v, radius, 0.0f, kTwoPi, kCircleSegments, color, duration, depthTest);
    // 胴の4本
    Line(start + u * radius, end + u * radius, color, duration, depthTest);
    Line(start - u * radius, end - u * radius, color, duration, depthTest);
    Line(start + v * radius, end + v * radius, color, duration, depthTest);
    Line(start - v * radius, end - v * radius, color, duration, depthTest);
    // 両端の半球（u と v の2つの面で半円）。end 側は forward 向き、start 側は逆向きにふくらむ
    AddArc(end, u, forward, radius, 0.0f, kPi, kHalfSegments, color, duration, depthTest);
    AddArc(end, v, forward, radius, 0.0f, kPi, kHalfSegments, color, duration, depthTest);
    AddArc(start, u, forward * -1.0f, radius, 0.0f, kPi, kHalfSegments, color, duration, depthTest);
    AddArc(start, v, forward * -1.0f, radius, 0.0f, kPi, kHalfSegments, color, duration, depthTest);
}

void DebugDraw::Grid(const Vector3& center, float size, int divisions, const Vector4& color, float duration, bool depthTest) {
    if (!IsEnabled() || divisions <= 0) {
        return;
    }
    const float half = size * 0.5f;
    const float step = size / static_cast<float>(divisions);
    for (int i = 0; i <= divisions; ++i) {
        const float offset = -half + step * static_cast<float>(i);
        Line(center + Vector3{ offset, 0.0f, -half }, center + Vector3{ offset, 0.0f, half }, color, duration, depthTest);
        Line(center + Vector3{ -half, 0.0f, offset }, center + Vector3{ half, 0.0f, offset }, color, duration, depthTest);
    }
}

void DebugDraw::Axes(const Matrix4x4& world, float size, float duration, bool depthTest) {
    if (!IsEnabled()) {
        return;
    }
    // 行ベクトルの書き方なので、0〜2行目が各軸、3行目が位置
    const Vector3 origin = { world.m[3][0], world.m[3][1], world.m[3][2] };
    const Vector4 colors[3] = { kRed, kGreen, kBlue };
    for (int i = 0; i < 3; ++i) {
        const Vector3 axis = { world.m[i][0], world.m[i][1], world.m[i][2] };
        if (Calculation::Length(axis) < kMinLength) {
            continue;
        }
        Arrow(origin, origin + Calculation::Normalize(axis) * size, colors[i], duration, depthTest);
    }
}

// ---------------------------------------------------------------------------
// 2D
// ---------------------------------------------------------------------------

void DebugDraw::Line2D(const Vector2& start, const Vector2& end, const Vector4& color, float duration, bool useCamera) {
    if (IsEnabled()) {
        instance->AddLine2D(start, end, color, duration, useCamera);
    }
}

void DebugDraw::Arrow2D(const Vector2& start, const Vector2& end, const Vector4& color, float duration, bool useCamera) {
    if (!IsEnabled()) {
        return;
    }
    Line2D(start, end, color, duration, useCamera);
    const Vector2 direction = end - start;
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length < kMinLength) {
        return;
    }
    const Vector2 forward = direction * (1.0f / length);
    const Vector2 side = { -forward.y, forward.x };
    const float headLength = length * kArrowHeadRatio;
    const Vector2 base = end - forward * headLength;
    const float headWidth = headLength * kArrowHeadWidth;
    Line2D(end, base + side * headWidth, color, duration, useCamera);
    Line2D(end, base - side * headWidth, color, duration, useCamera);
}

void DebugDraw::Cross2D(const Vector2& center, float size, const Vector4& color, float duration, bool useCamera) {
    if (!IsEnabled()) {
        return;
    }
    const float half = size * 0.5f;
    Line2D({ center.x - half, center.y }, { center.x + half, center.y }, color, duration, useCamera);
    Line2D({ center.x, center.y - half }, { center.x, center.y + half }, color, duration, useCamera);
}

void DebugDraw::Circle2D(const ::Circle& circle, const Vector4& color, float duration, bool useCamera) {
    if (!IsEnabled()) {
        return;
    }
    Vector2 previous = { circle.center.x + circle.radius, circle.center.y };
    for (int i = 1; i <= kCircleSegments; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(kCircleSegments);
        const Vector2 current = { circle.center.x + std::cos(angle) * circle.radius, circle.center.y + std::sin(angle) * circle.radius };
        Line2D(previous, current, color, duration, useCamera);
        previous = current;
    }
}

void DebugDraw::Rect2D(const Rect& rect, const Vector4& color, float duration, bool useCamera) {
    if (!IsEnabled()) {
        return;
    }
    const Vector2 topRight = { rect.max.x, rect.min.y };
    const Vector2 bottomLeft = { rect.min.x, rect.max.y };
    Line2D(rect.min, topRight, color, duration, useCamera);
    Line2D(topRight, rect.max, color, duration, useCamera);
    Line2D(rect.max, bottomLeft, color, duration, useCamera);
    Line2D(bottomLeft, rect.min, color, duration, useCamera);
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

template <typename Entry>
void DebugDraw::DrawLines(const std::vector<Entry>& lines, VertexBuffer& buffer, const Matrix4x4& viewProjection,
                          ID3D12PipelineState* pipeline) {
    if (lines.empty()) {
        return;
    }
    const size_t vertexCount = lines.size() * 2;
    EnsureCapacity(buffer, vertexCount);
    Vertex* vertex = buffer.data;
    for (const Entry& line : lines) {
        if constexpr (std::is_same_v<Entry, Line3DEntry>) {
            *vertex++ = { { line.start.x, line.start.y, line.start.z, 1.0f }, line.color };
            *vertex++ = { { line.end.x, line.end.y, line.end.z, 1.0f }, line.color };
        } else {
            *vertex++ = { { line.start.x, line.start.y, 0.0f, 1.0f }, line.color };
            *vertex++ = { { line.end.x, line.end.y, 0.0f, 1.0f }, line.color };
        }
    }

    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->SetPipelineState(pipeline);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(Matrix4x4) / sizeof(uint32_t), &viewProjection, 0);

    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer.resource->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * vertexCount);
    view.StrideInBytes = sizeof(Vertex);
    commandList->IASetVertexBuffers(0, 1, &view);
    commandList->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
}

template <typename Entry>
void DebugDraw::Age(std::vector<Entry>& lines, float deltaTime) {
    // 今描いたのが最後の線（残り 0 以下）を消し、残りは時間を減らす
    std::erase_if(lines, [](const Entry& line) { return line.remaining <= 0.0f; });
    for (Entry& line : lines) {
        line.remaining -= deltaTime;
    }
}

void DebugDraw::Flush3D() {
    if (!instance) {
        return;
    }
    DebugDraw& self = *instance;
    Camera* currentCamera = self.camera ? self.camera : CameraManager::GetInstance()->GetActiveCamera();
    if (currentCamera) {
        const Matrix4x4& viewProjection = currentCamera->GetViewProjectionMatrix();
        self.DrawLines(self.lines3D[static_cast<size_t>(Pass::kDepthTest)], self.buffers3D[static_cast<size_t>(Pass::kDepthTest)],
                       viewProjection, self.depthTestPipeline.Get());
        self.DrawLines(self.lines3D[static_cast<size_t>(Pass::kOnTop)], self.buffers3D[static_cast<size_t>(Pass::kOnTop)],
                       viewProjection, self.onTopPipeline.Get());
    }
    // カメラが無いシーン（2D のみ）でも、3D の線は時間どおりに消す
    for (std::vector<Line3DEntry>& lines : self.lines3D) {
        Age(lines, Time::GetUnscaledDeltaTime());
    }
}

void DebugDraw::Flush2D() {
    if (!instance) {
        return;
    }
    DebugDraw& self = *instance;
    // Sprite と同じく、ゲーム座標 → 今のビューポート（黒帯を除いたゲームの絵の範囲）
    const Vector2 gameSize = Screen::GetGameSize();
    const Matrix4x4 projection = Calculation::MakeOrthographicMatrix(0.0f, 0.0f, gameSize.x, gameSize.y, 0.0f, 100.0f);
    Camera2D* camera2D = Framework::GetInstance()->GetSpriteCommon()->GetCamera2D();
    const Matrix4x4 worldViewProjection = camera2D ? camera2D->GetViewMatrix() * projection : projection;

    self.DrawLines(self.lines2DWorld, self.buffer2DWorld, worldViewProjection, self.onTopPipeline.Get());
    self.DrawLines(self.lines2DScreen, self.buffer2DScreen, projection, self.onTopPipeline.Get());
    Age(self.lines2DWorld, Time::GetUnscaledDeltaTime());
    Age(self.lines2DScreen, Time::GetUnscaledDeltaTime());
}
