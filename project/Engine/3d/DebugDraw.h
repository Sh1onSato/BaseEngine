#pragma once
#include "Collision.h"
#include "DirectXCommon.h"
#include "Structs.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>

class Camera;

/// <summary>
/// 確認用の線を描く（当たり判定の形・レイ・向き・経路など）。どこからでも DebugDraw::Line(...) のように呼べる。
/// Framework が1つ持ち、SceneManager がシーンの Draw の後に描く（自分で Flush を呼ぶ必要はない）。
///
/// ・3D の線: シーンの 3D の後（UI より奥）に、アクティブなカメラ（SetCamera で変更可）で描く。
///   depthTest = true なら物に隠れる。false なら常に手前に見える
/// ・2D の線（Line2D など）: スプライトの後（UI より手前）に、ゲーム座標で描く。
///   useCamera = true なら SpriteCommon の 2D カメラに合わせて動く（2D カメラが無ければ画面の座標のまま）
/// ・duration（秒）: 0 ならそのフレームだけ。正の値なら、その秒数のあいだ毎フレーム描き続ける（1回だけ撃ったレイなど）。
///   時間はスローや一時停止に関係なく、実際の秒数で減る
/// ・Debug ビルドでは最初から有効、それ以外では無効（呼んでも何もしない）。SetEnabled で変えられる
///
/// 使い方の例:
///   DebugDraw::Line(start, end, DebugDraw::kRed);
///   DebugDraw::Sphere(collider->GetWorldSphere(), DebugDraw::kGreen);
///   DebugDraw::Arrow(position, position + velocity, DebugDraw::kYellow, 0.0f, false); // 隠れない矢印
///   DebugDraw::Rect2D(rect, DebugDraw::kCyan);
/// </summary>
class DebugDraw {
public:
    // よく使う色
    static constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
    static constexpr Vector4 kRed = { 1.0f, 0.2f, 0.2f, 1.0f };
    static constexpr Vector4 kGreen = { 0.2f, 1.0f, 0.2f, 1.0f };
    static constexpr Vector4 kBlue = { 0.3f, 0.5f, 1.0f, 1.0f };
    static constexpr Vector4 kYellow = { 1.0f, 0.9f, 0.2f, 1.0f };
    static constexpr Vector4 kCyan = { 0.2f, 1.0f, 1.0f, 1.0f };
    static constexpr Vector4 kMagenta = { 1.0f, 0.3f, 1.0f, 1.0f };
    static constexpr Vector4 kGray = { 0.6f, 0.6f, 0.6f, 1.0f };

    DebugDraw();
    ~DebugDraw();
    DebugDraw(const DebugDraw&) = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    // Framework が呼ぶ
    void Initialize(DirectXCommon* dxCommon);

    // --- 全体の設定 ---
    static void SetEnabled(bool enabled);
    static bool IsEnabled();
    // 3D の線を描くカメラ（所有しない）。nullptr なら CameraManager のアクティブなカメラ。
    // カメラを持つシーンが消える前に nullptr に戻すこと
    static void SetCamera(Camera* camera);
    // GameObjectManager の Draw で、当たり判定の形（緑=Dynamic 灰=Static 黄=Trigger 青=キネマティック）と
    // Rigidbody の速度（ピンクの矢印）を自動で描くか（既定 false。Debug ビルドでは [F7] で切り替え）
    static void SetColliderVisible(bool visible);
    static bool IsColliderVisible();
    // 積んだ線（duration 付きの物も）を全部消す。シーンの切り替え時に SceneManager が呼ぶ
    static void Clear();

    // --- 3D ---
    static void Line(const Vector3& start, const Vector3& end, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    // origin から direction（長さは問わない）の向きに length だけ伸ばした線
    static void Ray(const ::Ray& ray, float length, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    // 先に矢じりの付いた線
    static void Arrow(const Vector3& start, const Vector3& end, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    // 点の位置に小さな立体の十字（size は端から端までの長さ）
    static void Cross(const Vector3& center, float size, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    // normal に垂直な面の円
    static void Circle(const Vector3& center, const Vector3& normal, float radius, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    // 3方向の円で表す球
    static void Sphere(const ::Sphere& sphere, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    static void Box(const AABB& box, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    static void Box(const OBB& box, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    static void Capsule(const ::Capsule& capsule, const Vector4& color = kWhite, float duration = 0.0f, bool depthTest = true);
    // XZ 平面の格子（center を中心に、一辺 size の正方形を divisions 個に区切る）
    static void Grid(const Vector3& center, float size, int divisions, const Vector4& color = kGray, float duration = 0.0f, bool depthTest = true);
    // ワールド行列の位置に X（赤）・Y（緑）・Z（青）の軸
    static void Axes(const Matrix4x4& world, float size, float duration = 0.0f, bool depthTest = true);

    // --- 2D（ゲーム座標。y は下向き） ---
    static void Line2D(const Vector2& start, const Vector2& end, const Vector4& color = kWhite, float duration = 0.0f, bool useCamera = true);
    static void Arrow2D(const Vector2& start, const Vector2& end, const Vector4& color = kWhite, float duration = 0.0f, bool useCamera = true);
    static void Cross2D(const Vector2& center, float size, const Vector4& color = kWhite, float duration = 0.0f, bool useCamera = true);
    static void Circle2D(const ::Circle& circle, const Vector4& color = kWhite, float duration = 0.0f, bool useCamera = true);
    static void Rect2D(const Rect& rect, const Vector4& color = kWhite, float duration = 0.0f, bool useCamera = true);

    // --- SceneManager が呼ぶ ---
    // 3D の線を今の描画先（シーンの 3D の描画先と深度）に描く
    static void Flush3D();
    // 2D の線を今の描画先に描く（スプライトの後）
    static void Flush2D();

    // 今積まれている線の数（確認表示用）
    static size_t GetLineCount();

private:
    struct Vertex {
        Vector4 position;
        Vector4 color;
    };
    struct Line3DEntry {
        Vector3 start;
        Vector3 end;
        Vector4 color;
        float remaining; // 残り秒数（0 以下ならこのフレームで最後）
    };
    struct Line2DEntry {
        Vector2 start;
        Vector2 end;
        Vector4 color;
        float remaining;
    };
    // GPU に渡す頂点の置き場（足りなくなったら広げる）
    struct VertexBuffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Vertex* data = nullptr;
        size_t capacity = 0; // 頂点の数
    };
    // 描き分ける種類
    enum class Pass {
        kDepthTest, // 3D・物に隠れる
        kOnTop,     // 3D・隠れない
        kCount,
    };

    void CreateRootSignature();
    void CreatePipelineState(bool depthTest, Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPipeline);

    void AddLine3D(const Vector3& start, const Vector3& end, const Vector4& color, float duration, bool depthTest);
    void AddLine2D(const Vector2& start, const Vector2& end, const Vector4& color, float duration, bool useCamera);
    // lines の頂点を buffer に書いて、行列 viewProjection・PSO pipeline で描く
    template <typename Entry>
    void DrawLines(const std::vector<Entry>& lines, VertexBuffer& buffer, const Matrix4x4& viewProjection, ID3D12PipelineState* pipeline);
    void EnsureCapacity(VertexBuffer& buffer, size_t vertexCount);
    // 描いた後に時間を進め、時間切れの線を消す
    template <typename Entry>
    static void Age(std::vector<Entry>& lines, float deltaTime);
    // 1種類の上限を超えたら足さない（消し忘れで増え続けたとき用。超えたら1回だけログ）
    bool HasRoom(size_t currentCount);

    static DebugDraw* instance;

    DirectXCommon* dxCommon = nullptr;
    bool isEnabled = false;
    bool isColliderVisible = false;
    Camera* camera = nullptr;

    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> depthTestPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> onTopPipeline; // 3D の隠れない線と 2D の線

    std::vector<Line3DEntry> lines3D[static_cast<size_t>(Pass::kCount)];
    std::vector<Line2DEntry> lines2DWorld;  // 2D カメラに合わせる
    std::vector<Line2DEntry> lines2DScreen; // 画面に固定
    VertexBuffer buffers3D[static_cast<size_t>(Pass::kCount)];
    VertexBuffer buffer2DWorld;
    VertexBuffer buffer2DScreen;
    bool hasWarnedLimit = false;

    static constexpr size_t kMaxLinesPerList = 200000;
    static constexpr size_t kInitialVertexCapacity = 4096;
    static constexpr int kCircleSegments = 32;
};
