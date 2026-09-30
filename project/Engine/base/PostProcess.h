#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "Structs.h"
#include <wrl.h>
#include <memory>

/// <summary>
/// ポストエフェクト（オフスクリーンレンダリング）管理クラス
/// </summary>
class PostProcess {
public:
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    // 描画開始（これ以降の描画はテクスチャに行われる）
    void PreDraw();
    // 描画終了（これ以降の描画は画面に行われる）
    void PostDraw();

    // 更新（showDebugUI: trueの間だけImGui調整パネルを表示する。Pキーでのポーズ状態と連動させる想定）
    void Update(bool showDebugUI);

    // 結果を画面に表示する
    void Draw();

    // グレースケール表示のON/OFFを切り替える（キー入力等から呼び出す想定）
    void ToggleGrayscale() { grayscaleEnabled = !grayscaleEnabled; }
    // セピア表示のON/OFFを切り替える（キー入力等から呼び出す想定。グレースケールと同時ONの場合はセピアを優先）
    void ToggleSepia() { sepiaEnabled = !sepiaEnabled; }
    // 画面全体ぼかしのON/OFFを切り替える（キー入力等から呼び出す想定。ブルームとは独立して重ねがけ可能）
    void ToggleBlur() { blurEnabled = !blurEnabled; }
    // ワープ通過時などに一瞬だけグレースケールを掛ける演出を開始する（手動トグルのgrayscaleEnabledとは独立）
    void TriggerFlash() { grayscaleFlashTimer = kFlashDuration; }
    // 画面周辺減光（ヴィネット）のON/OFFを切り替える（キー入力等から呼び出す想定）
    void ToggleVignette() { vignetteEnabled = !vignetteEnabled; }
    // 被弾時の一瞬赤ヴィネット演出を開始する（TakeDamage()から呼ぶ想定）
    void TriggerImpactVignette() { damageVignetteTimer = kImpactVignetteDuration; }
    // 画面全体ぼかし（ボックスフィルター版）のON/OFFを切り替える（ガウシアン版のToggleBlur()と対称）
    void ToggleBoxBlur() { boxBlurEnabled = !boxBlurEnabled; }

    // --- 設定メニュー（SettingsMenu）から値を読み書きするためのアクセサ ---
    float GetBloomIntensity() const { return bloomIntensity; }
    void SetBloomIntensity(float v) { bloomIntensity = v; }
    bool IsBlurEnabled() const { return blurEnabled; }
    float GetScreenBlurStrength() const { return screenBlurStrength; }
    void SetScreenBlurStrength(float v) { screenBlurStrength = v; }
    bool IsBoxBlurEnabled() const { return boxBlurEnabled; }
    float GetBoxBlurStrength() const { return boxBlurStrength; }
    void SetBoxBlurStrength(float v) { boxBlurStrength = v; }
    bool IsGrayscaleEnabled() const { return grayscaleEnabled; }
    bool IsSepiaEnabled() const { return sepiaEnabled; }
    bool IsVignetteEnabled() const { return vignetteEnabled; }
    float GetVignetteIntensity() const { return vignetteIntensity; }
    void SetVignetteIntensity(float v) { vignetteIntensity = v; }
    float GetVignetteRadius() const { return vignetteRadius; }
    void SetVignetteRadius(float v) { vignetteRadius = v; }

private:
    // RTVインデックス（rtvDescriptorHeapの並び）
    static constexpr uint32_t kSceneRtvIndex = 2;
    static constexpr uint32_t kBrightnessRtvIndex = 3;
    static constexpr uint32_t kBlurHRtvIndex = 4;
    static constexpr uint32_t kBlurVRtvIndex = 5;
    static constexpr uint32_t kFullBlurHRtvIndex = 6;
    static constexpr uint32_t kFullBlurVRtvIndex = 7;

    // ブルーム処理は負荷軽減のため半解像度で行う
    static constexpr uint32_t kBloomWidth = WinApp::KclientWidth / 2;
    static constexpr uint32_t kBloomHeight = WinApp::KclientHeight / 2;

    void CreateGraphicsPipeline();
    void CreateExtractPipeline();
    void CreateBlurPipeline();

    // 指定した幅・高さのテクスチャ1枚を作成するヘルパー（輝度抽出/ブラーHV/全画面ぼかし共通の手順を集約）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBloomTexture(uint32_t width, uint32_t height, uint32_t rtvIndex, uint32_t& outSrvIndex);

    void DrawBrightnessExtractPass();
    // useBoxFilter: trueならガウシアンの代わりにボックスフィルター（均等重み平均）のPSOを使う
    void DrawGaussianBlurPass(bool horizontal, ID3D12Resource* srcResource, uint32_t srcSrvIndex, ID3D12Resource* dstResource, uint32_t dstRtvIndex,
        D3D12_VIEWPORT viewport, D3D12_RECT scissorRect, float texelWidth, float texelHeight, float strength, bool useBoxFilter = false);
    void DrawCompositePass();
    // 画面全体ぼかしパス（blurEnabled時にシーンテクスチャを水平→垂直の2パスでぼかし、fullBlurVResourceに結果を出す）
    void DrawFullScreenBlurPass();

    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;

    // オフスクリーン描画用のリソース（フル解像度シーン）
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    uint32_t rtvIndex = kSceneRtvIndex;
    uint32_t srvIndex = 0;

    // 輝度抽出結果（半解像度）
    Microsoft::WRL::ComPtr<ID3D12Resource> brightnessResource;
    uint32_t brightnessSrvIndex = 0;

    // 水平ブラー結果（半解像度）
    Microsoft::WRL::ComPtr<ID3D12Resource> blurHResource;
    uint32_t blurHSrvIndex = 0;

    // 垂直ブラー結果（半解像度）＝最終ブルームテクスチャ
    Microsoft::WRL::ComPtr<ID3D12Resource> blurVResource;
    uint32_t blurVSrvIndex = 0;

    // 画面全体ぼかし用・水平ブラー結果（フル解像度）
    Microsoft::WRL::ComPtr<ID3D12Resource> fullBlurHResource;
    uint32_t fullBlurHSrvIndex = 0;

    // 画面全体ぼかし用・垂直ブラー結果（フル解像度）＝最終ぼかしテクスチャ
    Microsoft::WRL::ComPtr<ID3D12Resource> fullBlurVResource;
    uint32_t fullBlurVSrvIndex = 0;

    // 輝度抽出パイプライン
    Microsoft::WRL::ComPtr<ID3D12RootSignature> extractRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> extractPipelineState;

    // ガウシアンブラーパイプライン（水平/垂直共通）
    Microsoft::WRL::ComPtr<ID3D12RootSignature> blurRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPipelineState;
    // ボックスフィルターパイプライン（blurRootSignatureを共用。画面全体ぼかしでガウシアンと切り替え比較できる）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> boxBlurPipelineState;

    // 合成パイプライン（シーン + ブルーム）
    Microsoft::WRL::ComPtr<ID3D12RootSignature> compositeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePipelineState;

    // 半解像度パス用のビューポート・シザー矩形
    D3D12_VIEWPORT bloomViewport{};
    D3D12_RECT bloomScissorRect{};

    // ImGuiで調整するブルームパラメータ
    float bloomThreshold = 1.0f;
    float bloomIntensity = 1.0f;
    float blurStrength = 1.0f;
    bool grayscaleEnabled = false; // 合成パスで画面全体をグレースケール化するかどうか
    bool sepiaEnabled = false;     // 合成パスで画面全体をセピア調にするかどうか（グレースケールより優先）

    // ワープ通過時の一瞬グレースケール演出（三角波：前半でフェードイン、後半でフェードアウト）
    float grayscaleFlashTimer = 0.0f; // 残り時間(秒)。0になると演出は終わる
    static constexpr float kFlashDuration = 0.25f;

    // 画面全体ぼかし（ブルームとは独立管理。ガウシアン=Bキー、ボックスはImGuiでそれぞれ独立にON/OFF。
    // 両方ONの場合はボックスを優先する。ボックスは全タップ均等重みでガウシアンより効きが強いため別々のstrengthを持つ）
    bool blurEnabled = false;
    float screenBlurStrength = 2.0f;
    bool boxBlurEnabled = false;
    float boxBlurStrength = 0.4f;

    // 画面周辺減光（ヴィネット。Vキーでトグル。調整用に常設）
    bool vignetteEnabled = false;
    float vignetteIntensity = 0.8f; // 0=効果なし、1で端が真っ黒
    float vignetteRadius = 0.5f;    // ここより中心側は減光しない範囲（0=中心から、1で端のみ）

    // 被弾時の一瞬赤ヴィネット演出（三角波：前半でフェードイン、後半でフェードアウト。上のvignetteとは独立）
    float damageVignetteTimer = 0.0f; // 残り時間(秒)。0になると演出は終わる
    static constexpr float kImpactVignetteDuration = 0.4f;
    static constexpr float kImpactVignetteMaxBlend = 0.4f; // 発生時でも画面が完全な赤一色にならないよう上限を設ける

    // 頂点データ (画面全体を覆う板ポリ用)
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

};
