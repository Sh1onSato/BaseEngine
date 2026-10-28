#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "Structs.h"
#include <cstdint>
#include <wrl.h>

/// <summary>
/// 画面の効果（ポストエフェクト）の設定。値を変えるだけで効く（全部の値が既定のままなら何もしない）。
/// </summary>
struct PostEffectSettings {
    // ブルーム: 明るい所の光がにじんで広がる
    bool bloomEnabled = false;
    float bloomThreshold = 0.8f; // これより明るい所（0〜1、明るさ）だけが光る。下げるほど広い範囲が光る
    float bloomIntensity = 1.0f; // 光を足す強さ
    float bloomRadius = 1.5f;    // にじむ広さ（大きいほど広くぼける。内部解像度を変えても画面上の広さは同じ）

    // 接している所を暗くする（SSAO）: 物と床が接する所・すき間・角に周りの光が届きにくいのを表す。「浮いて見える」のを防ぐ
    bool ambientOcclusionEnabled = false;
    float ambientOcclusionRadius = 0.5f;    // 調べる範囲（ワールドの長さ。物の大きさの半分くらいまで）
    float ambientOcclusionIntensity = 1.0f; // 暗くする強さ（0〜2 くらい）

    // 画面全体のぼかし（0 で無し。ポーズ画面の背景など。2〜3 で強め。内部解像度を変えても画面上のぼけ方は同じ）
    float blur = 0.0f;

    // 色の調整
    float brightness = 0.0f;  // 明るさ（-1〜1。0 でそのまま）
    float contrast = 1.0f;    // コントラスト（1 でそのまま、0 で灰色一色）
    float saturation = 1.0f;  // 彩度（1 でそのまま、0 で白黒、2 で鮮やか）
    float sepia = 0.0f;       // セピア（0〜1）
    Vector3 tint = { 1.0f, 1.0f, 1.0f }; // 掛ける色（夕方なら { 1.0f, 0.85f, 0.7f } など）

    // ヴィネット: 画面の端を暗く（または vignetteColor に）する。被弾の赤い縁なら色を赤にして強さを動かす
    float vignetteIntensity = 0.0f; // 0 で無し、1 で端が vignetteColor 一色
    float vignetteRadius = 0.5f;    // ここより中心側は変えない（0 で中心から、1 で角だけ）
    Vector3 vignetteColor = { 0.0f, 0.0f, 0.0f };

    // どれか1つでも効果があるか（無ければ PostProcess は何もしない）
    bool IsActive() const;
};

/// <summary>
/// 画面の効果（ブルーム・ぼかし・色の調整・ヴィネット）。3D の絵にだけ掛かり、UI・文字には掛からない。
/// Framework が持っていて、Framework::GetInstance()->GetPostProcess() で使う。
///
/// 使い方:
///   PostEffectSettings& effect = Framework::GetInstance()->GetPostProcess()->GetSettings();
///   effect.bloomEnabled = true;           // このシーンだけブルーム
///   effect.saturation = 0.0f;             // 白黒にする
/// シーンを切り替えると、既定の設定（SetDefaultSettings。最初は全部効果なし）に戻る。
/// ゲーム全体でいつも掛けたい効果は MyGame::Initialize で SetDefaultSettings する。
/// Debug ビルドでは [F6] で調整用の ImGui の窓を出せる。
///
/// しくみ: 効果があるフレームは、RenderScaler が 3D を画面ではなく別の描画先に描き、
/// RenderScaler::EndScene の中で Render を呼んで、効果を掛けながら画面（黒帯の内側）に写す。
/// 内部解像度を下げているときは、小さい解像度のまま効果を掛けてから引き伸ばす（その分軽い）。
/// </summary>
class PostProcess {
public:
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    // 今の設定（シーンから書き換える）
    PostEffectSettings& GetSettings() { return settings; }
    const PostEffectSettings& GetSettings() const { return settings; }
    // シーンを切り替えたときに戻る設定。今の設定も同じにする
    void SetDefaultSettings(const PostEffectSettings& newDefault);
    const PostEffectSettings& GetDefaultSettings() const { return defaultSettings; }
    // 今の設定を既定に戻す（SceneManager がシーンの切り替えで呼ぶ）
    void ResetToDefault() { settings = defaultSettings; }

    // このフレームに効果を掛けるか（RenderScaler が別の描画先に描くかを決めるのに使う）
    bool IsActive() const { return settings.IsActive(); }

    /// <summary>
    /// source（width x height の 3D の絵。PIXEL_SHADER_RESOURCE 状態）に効果を掛けて、
    /// 今のメインの描画先（バックバッファの黒帯の内側）に写す。RenderScaler::EndScene が呼ぶ。
    /// usePointFilter: 引き伸ばすときにくっきり（ドット絵向け）にするか
    /// depthSrvIndex: 3D を描いたときの深度（PIXEL_SHADER_RESOURCE 状態。SSAO に使う。無ければ kNoDepth で SSAO をしない）
    /// </summary>
    void Render(uint32_t sourceSrvIndex, uint32_t width, uint32_t height, bool usePointFilter, uint32_t depthSrvIndex = kNoDepth);
    // 今の設定で深度を使うか（RenderScaler が深度を読める状態にするかを決める）
    bool UsesDepth() const { return settings.ambientOcclusionEnabled && settings.ambientOcclusionIntensity > 0.0f; }
    static constexpr uint32_t kNoDepth = UINT32_MAX;

    // 調整用の ImGui の窓（Debug ビルドで Framework が [F6] のときに呼ぶ）
    void DrawDebugUI();

private:
    // 途中の絵1枚（描画先と、読むときの SRV）
    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t rtvIndex = 0;
        uint32_t srvIndex = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    enum TargetId {
        kBright,     // 明るい所だけ（半分の解像度）
        kBloomPing,  // ブルームのぼかしの途中（半分の解像度）
        kBloomPong,  // ブルームのぼかしの結果（半分の解像度）
        kBlurPing,   // 画面全体のぼかしの途中（等倍）
        kBlurPong,   // 画面全体のぼかしの結果（等倍）
        kAo,         // 接している所の暗さ（半分の解像度）
        kAoPing,     // その ぼかしの途中・結果（ノイズを消す）
        kAoPong,
        kTargetCount,
    };

    void CreateRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipeline(const wchar_t* pixelShaderPath);
    // 途中の絵を width x height（ブルームは半分）で作り直す（同じ大きさなら何もしない）
    void EnsureTargets(uint32_t width, uint32_t height);
    void CreateTarget(Target& target, uint32_t width, uint32_t height);

    // source を読んで target に全面を描く。constants はピクセルシェーダーの b0
    void DrawPass(ID3D12PipelineState* pipeline, uint32_t sourceSrvIndex, Target& target, const float* constants, uint32_t constantCount);
    // source を target 経由で横・縦にぼかし、結果を result に入れる
    void Blur(uint32_t sourceSrvIndex, Target& ping, Target& result, float radius);

    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;

    PostEffectSettings settings;
    PostEffectSettings defaultSettings;

    Target targets[kTargetCount];
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;

    // ルートシグネチャ（全部の段で共通）: [0] t0 読む絵 [1] t1 2枚目の絵（合成のときのブルーム）[2] t2 3枚目の絵（合成のときの SSAO）
    // [3] b0 定数。s0 なめらか / s1 くっきり
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> extractPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ambientOcclusionPipeline;
    static constexpr UINT kRootIndexConstants = 3;

    // 途中の絵の RTV ヒープの番号（RTV: 0〜1 スワップチェーン、2〜6 PostProcess、8 RenderScaler、9〜11 PostProcess の SSAO）
    static constexpr uint32_t kFirstRtvIndex = 2;
    static constexpr uint32_t kFirstAoRtvIndex = 9;
    // b0 の定数の数（合成の段が一番多い。PostProcess.PS.hlsl の CompositeParams と同じ並び）
    static constexpr uint32_t kRootConstantCount = 16;
};
