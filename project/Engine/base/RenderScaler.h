#pragma once
#include "DirectXCommon.h"
#include "PostProcess.h"
#include "SrvManager.h"
#include <cstdint>
#include <wrl.h>

/// <summary>
/// 内部解像度スケール。3D を画面より小さい解像度で描いてから画面いっぱいに引き伸ばし、重いシーンでも速く動くようにする。
/// UI・文字（SceneManager がシーンの Draw の後に描くスプライト）と ImGui は、引き伸ばした後に画面の解像度でくっきり描かれる。
/// Framework が持っていて、Framework::GetInstance()->GetRenderScaler() で使う。
///
/// 使い方:
///   RenderScaler* scaler = Framework::GetInstance()->GetRenderScaler();
///   scaler->SetScale(0.5f);                                   // 縦横それぞれ半分（ピクセル数は 1/4）で 3D を描く
///   scaler->SetFilter(RenderScaler::Filter::kPoint);          // ドット絵風にくっきり引き伸ばす（既定はなめらか）
/// Debug ビルドでは [F9] で 1.0 → 0.75 → 0.5 → 0.25 と切り替えられる。
///
/// 注意:
///   - シーンの中で、シャドウパスなど別の描画先にした後は dxCommon->BindMainRenderTarget() で戻す
///     （バックバッファの RTV を直接 OMSetRenderTargets しない。小さい描画先に描けなくなる）
///   - 3D より奥に出すためにシーンの中で Flush したスプライトは、3D と一緒に小さい解像度で描かれる
/// </summary>
class RenderScaler {
public:
    // 引き伸ばすときの見え方
    enum class Filter {
        kLinear, // なめらか（ぼやける）
        kPoint,  // くっきり（ドット絵向け。倍率が 0.5 や 0.25 だとピクセルがそろってきれい）
    };

    static constexpr float kMinScale = 0.25f;
    static constexpr float kMaxScale = 1.0f;

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    // 画面の効果（所有しない）。効果があるフレームは倍率 1 でも別の描画先に描き、EndScene で PostProcess::Render を通して写す
    void SetPostProcess(PostProcess* newPostProcess) { postProcess = newPostProcess; }

    // 3D を描く解像度の倍率（0.25〜1.0。1.0 なら今までどおり画面に直接描く）
    void SetScale(float scale);
    float GetScale() const { return scale; }
    void SetFilter(Filter newFilter) { filter = newFilter; }
    Filter GetFilter() const { return filter; }

    // シーンを描く前に呼ぶ（MyGame::Draw の dxCommon->PreDraw・srvManager->PreDraw の後）。
    // 倍率が 1 未満なら、メインの描画先を小さい描画先に切り替える
    void BeginScene();
    // シーンの 3D を描き終えた後に呼ぶ（SceneManager::Draw がシーンの Draw の後に呼ぶ）。
    // 小さい描画先を画面に引き伸ばして写し、メインの描画先をバックバッファに戻す。倍率 1 なら何もしない
    void EndScene();

    // 今 3D を描いている解像度（倍率 1 ならバックバッファ内のゲームの絵の大きさ）
    uint32_t GetRenderWidth() const { return renderWidth; }
    uint32_t GetRenderHeight() const { return renderHeight; }

private:
    // 小さい描画先が使う RTV・DSV ヒープの番号（RTV: 0〜1 スワップチェーン、2〜7 PostProcess。DSV: 0 メイン、1 シャドウマップ）
    static constexpr uint32_t kRtvIndex = 8;
    static constexpr uint32_t kDsvIndex = 2;

    void CreatePipeline();
    // 小さい描画先（色と深度）を width x height で作り直す（同じ大きさなら何もしない）
    void EnsureTarget(uint32_t width, uint32_t height);

    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;
    float scale = 1.0f;
    Filter filter = Filter::kLinear;

    PostProcess* postProcess = nullptr;
    // 今のフレームで画面とは別の描画先に描いているか（倍率 1 未満か、画面の効果があるとき）
    bool isOffscreen = false;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> colorResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthResource;
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    uint32_t srvIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
};
