#pragma once
#include "Calculation.h"
#include "Structs.h"
#include "ParticleCommon.h"
#include "Camera.h"
#include <vector>
#include <list>
#include <wrl.h>

// 線(Line)専用パラメータの既定値（Particle/ParticleEmitParams/EmitterSettingの3構造体で
// 同じ値がそれぞれ独立に書かれていたのをここに集約）
namespace ParticleLineDefaults {
    constexpr float kLineWidth = 0.05f;
    constexpr float kLineLengthMultiplier = 10.0f;
    constexpr float kMaxLineLength = 1000.0f;
}

/// <summary>
/// 個々のパーティクル（粒）のデータ
/// </summary>
struct Particle {
    Transform transform;
    Vector3 velocity;
    Vector4 color;
    float lifeTime; // 寿命（秒）
    float currentTime; // 現在の経過時間

    // --- 見た目の時間変化 ---
    Vector3 startScale = { 1.0f, 1.0f, 1.0f };
    Vector3 endScale = { 1.0f, 1.0f, 1.0f };
    Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    bool useColorGradient = false;

    // --- スプライトシートアニメーション ---
    uint32_t spriteSheetColumns = 1;
    uint32_t spriteSheetRows = 1;
    bool loopSpriteAnimation = false;

    // --- 速度・物理表現 ---
    Vector3 gravity = { 0.0f, 0.0f, 0.0f }; // 毎フレーム速度に加算される重力
    float drag = 0.0f;                      // 空気抵抗(0〜1)

    // --- 線(Line)専用パラメータ ---
    float lineWidth = ParticleLineDefaults::kLineWidth;                       // 線の太さ
    float lineLengthMultiplier = ParticleLineDefaults::kLineLengthMultiplier; // 線の長さ = その時点の速さ × この値
    float maxLineLength = ParticleLineDefaults::kMaxLineLength;               // 線の長さの上限（速すぎて伸びすぎるのを防ぐ）

    // --- 点・面(ビルボード)専用パラメータ ---
    float roll = 0.0f;  // ビルボード面内での回転角（ラジアン）。粒ごとに向きをばらつかせたい時に使う
    float shear = 0.0f; // 平行四辺形のような歪み量。0で歪みなし、値を入れるとX方向にせん断される
};

/// <summary>
/// パーティクル発生時に渡すパラメータ一式
/// </summary>
struct ParticleEmitParams {
    Vector3 position;
    Vector3 velocity;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f }; // useColorGradient=falseの場合に使用
    float lifeTime = 1.0f;

    Vector3 startScale = { 1.0f, 1.0f, 1.0f };
    Vector3 endScale = { 1.0f, 1.0f, 1.0f };
    Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    bool useColorGradient = false;

    uint32_t spriteSheetColumns = 1;
    uint32_t spriteSheetRows = 1;
    bool loopSpriteAnimation = false;

    Vector3 gravity = { 0.0f, 0.0f, 0.0f };
    float drag = 0.0f;

    float lineWidth = ParticleLineDefaults::kLineWidth;
    float lineLengthMultiplier = ParticleLineDefaults::kLineLengthMultiplier;
    float maxLineLength = ParticleLineDefaults::kMaxLineLength;

    float roll = 0.0f;
    float shear = 0.0f;
};

/// <summary>
/// GPUに送る用のパーティクルデータ
/// </summary>
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
    Vector2 uvOffset;
    Vector2 uvScale;
};

/// <summary>
/// パーティクルマネージャー
/// </summary>
class ParticleManager {
public:
    // 1つのマネージャーで管理できる最大数
    static const uint32_t kMaxParticles = 50000;

    void Initialize(ParticleCommon* common, uint32_t textureIndex, ParticleDrawType drawType = ParticleDrawType::kTriangle);
    void Update();
    void Draw();

    // パーティクルの追加（拡張版：見た目変化・スプライトシート対応）
    void Emit(const ParticleEmitParams& params);

    // パーティクルの追加（互換用の簡易版）
    void Emit(const Vector3& position, const Vector3& velocity, const Vector4& color, float lifeTime);

    // ゲッター
    ParticleDrawType GetDrawType() const { return drawType; }

private:
    ParticleCommon* common = nullptr;
    uint32_t textureIndex = 0;
    ParticleDrawType drawType = ParticleDrawType::kTriangle;

    // 現在のアクティブなパーティクル数
    uint32_t instanceCount = 0;

    // パーティクルのリスト
    std::list<Particle> particles;

    // インスタンシング用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;
    ParticleForGPU* instancingData = nullptr;

    // 頂点データ (パーティクルはただの板ポリ)
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
};
