#pragma once
#include "Calculation.h"
#include "ParticleManager.h"
#include <string>
#include <random>

/// <summary>
/// パーティクルの発生形状
/// </summary>
enum class EmitShape {
    kBox,    // 直方体（従来通りminVelocity~maxVelocityの範囲でランダム）
    kSphere, // 球の内部（速度は中心から外向き）
    kCircle, // 円（XZ平面上、速度は外向き）
    kCone,   // コーン（指定した方向を中心にある角度範囲へ広がる）
};

/// <summary>
/// パーティクル発生源
/// </summary>
class ParticleEmitter {
public:
    struct EmitterSetting {
        Vector3 position;       // 発生中心座標
        Vector3 minVelocity;    // 最小初速 (kBoxのみ使用)
        Vector3 maxVelocity;    // 最大初速 (kBoxのみ使用)
        Vector4 color;          // 色 (startColor/endColorを使わない場合のデフォルト)
        float lifeTime;         // 寿命
        uint32_t count;         // 一度に出す数
        float frequency;        // 発生間隔（秒）

        // --- 発生形状 ---
        EmitShape shape = EmitShape::kBox;
        float radius = 1.0f;          // kSphere/kCircle/kConeの半径・発生半径
        float minSpeed = 1.0f;        // kSphere/kCircle/kConeの初速の最小値
        float maxSpeed = 1.0f;        // kSphere/kCircle/kConeの初速の最大値
        Vector3 direction = { 0.0f, 1.0f, 0.0f }; // kConeの中心方向
        float coneAngle = 0.3f;       // kConeの広がり角（ラジアン、中心軸からの半角）

        // --- 見た目の時間変化 ---
        Vector3 startScale = { 1.0f, 1.0f, 1.0f }; // 発生直後のスケール倍率
        Vector3 endScale = { 1.0f, 1.0f, 1.0f };   // 消滅直前のスケール倍率
        Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 発生直後の色（startColorを使う場合colorは無視）
        Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };   // 消滅直前の色
        bool useColorGradient = false; // trueならstartColor/endColorで補間、falseなら従来通りcolor+フェードアウト

        // --- スプライトシートアニメーション ---
        uint32_t spriteSheetColumns = 1; // スプライトシートの列数
        uint32_t spriteSheetRows = 1;    // スプライトシートの行数
        bool loopSpriteAnimation = false; // trueなら寿命中に複数回ループ再生する

        // --- 速度・物理表現 ---
        float speedMultiplier = 1.0f;            // 生成された初速全体に掛ける倍率。全体的に速く/遅くしたい時はここを調整
        Vector3 gravity = { 0.0f, 0.0f, 0.0f };  // 毎フレーム速度に加算される重力ベクトル
        float drag = 0.0f;                       // 空気抵抗。毎フレーム速度に(1-drag)を掛けて減速させる(0.0=減速なし, 1.0=即停止)

        // --- サイズのばらつき ---
        float sizeVarianceMin = 1.0f; // 個体ごとのサイズ倍率の最小値
        float sizeVarianceMax = 1.0f; // 個体ごとのサイズ倍率の最大値（point/triangleは全体サイズ、lineは太さに反映）

        // --- 線(Line)専用パラメータ ---
        float lineWidth = ParticleLineDefaults::kLineWidth;                       // 線の太さ（X/Z方向のスケール基準値）
        float lineLengthMultiplier = ParticleLineDefaults::kLineLengthMultiplier; // 線の長さ = その時点の速さ × この値。速度が変化すると長さも追従して変わる
        float maxLineLength = ParticleLineDefaults::kMaxLineLength;               // 線の長さの上限
    };

    void Initialize(const std::string& name, ParticleManager* manager, const EmitterSetting& setting);
    void Update();

    // ゲッター・セッター
    const std::string& GetName() const { return name; }
    void SetPosition(const Vector3& pos) { setting.position = pos; }

    const EmitterSetting& GetSetting() const { return setting; }
    void SetSetting(const EmitterSetting& newSetting) { setting = newSetting; }

private:
    Vector3 GenerateVelocity();
    Vector3 GeneratePosition();

private:
    std::string name;
    ParticleManager* manager = nullptr;
    EmitterSetting setting;

    float timer = 0.0f;

    // 乱数生成器
    std::mt19937 randomEngine;
};
