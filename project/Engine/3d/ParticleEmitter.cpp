#include "ParticleEmitter.h"
#include <random>
#include <cmath>

void ParticleEmitter::Initialize(const std::string& name, ParticleManager* manager, const EmitterSetting& setting) {
    this->name = name;
    this->manager = manager;
    this->setting = setting;
    this->timer = 0.0f;

    // 乱数生成器の初期化
    std::random_device seed_gen;
    randomEngine.seed(seed_gen());
}

void ParticleEmitter::Update() {
    timer += Calculation::kFixedDeltaTime; // 暫定固定フレームレート前提（本来は実フレーム時間を受け取るべき）

    if (timer >= setting.frequency) {
        timer -= setting.frequency;

        // 指定された数だけ放出
        for (uint32_t i = 0; i < setting.count; ++i) {
            ParticleEmitParams params;
            params.position = GeneratePosition();

            // 速度を生成し、全体速度倍率を適用（「全体的に速くしたい」はここを調整すればOK）
            Vector3 velocity = GenerateVelocity();
            velocity = {
                velocity.x * setting.speedMultiplier,
                velocity.y * setting.speedMultiplier,
                velocity.z * setting.speedMultiplier,
            };
            params.velocity = velocity;

            params.color = setting.color;
            params.lifeTime = setting.lifeTime;

            // 個体ごとのサイズばらつき（同じ設定でも粒ごとに大きさが変わるようにする）
            std::uniform_real_distribution<float> distSize(setting.sizeVarianceMin, setting.sizeVarianceMax);
            float sizeVariance = distSize(randomEngine);

            params.startScale = {
                setting.startScale.x * sizeVariance,
                setting.startScale.y * sizeVariance,
                setting.startScale.z * sizeVariance,
            };
            params.endScale = {
                setting.endScale.x * sizeVariance,
                setting.endScale.y * sizeVariance,
                setting.endScale.z * sizeVariance,
            };
            params.startColor = setting.startColor;
            params.endColor = setting.endColor;
            params.useColorGradient = setting.useColorGradient;

            params.spriteSheetColumns = setting.spriteSheetColumns;
            params.spriteSheetRows = setting.spriteSheetRows;
            params.loopSpriteAnimation = setting.loopSpriteAnimation;

            // 物理パラメータ（重力・空気抵抗）
            params.gravity = setting.gravity;
            params.drag = setting.drag;

            // 線専用：太さにもサイズばらつきを反映。長さは速度から毎フレーム計算するのでここでは倍率だけ渡す
            params.lineWidth = setting.lineWidth * sizeVariance;
            params.lineLengthMultiplier = setting.lineLengthMultiplier;
            params.maxLineLength = setting.maxLineLength;

            manager->Emit(params);
        }
    }
}

Vector3 ParticleEmitter::GeneratePosition() {
    switch (setting.shape) {
    case EmitShape::kSphere: {
        // 球内部のランダムな点（一様分布）
        std::uniform_real_distribution<float> distR(0.0f, 1.0f);
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * Calculation::kPi);
        std::uniform_real_distribution<float> distZ(-1.0f, 1.0f);

        float u = distR(randomEngine);
        float r = setting.radius * std::cbrt(u); // 体積一様分布になるよう立方根を取る
        float theta = distAngle(randomEngine);
        float z = distZ(randomEngine);
        float xyScale = std::sqrt((std::max)(0.0f, 1.0f - z * z));

        Vector3 offset = {
            r * xyScale * std::cos(theta),
            r * xyScale * std::sin(theta),
            r * z,
        };
        return { setting.position.x + offset.x, setting.position.y + offset.y, setting.position.z + offset.z };
    }
    case EmitShape::kCircle: {
        // XZ平面上の円内のランダムな点
        std::uniform_real_distribution<float> distR(0.0f, 1.0f);
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * Calculation::kPi);

        float r = setting.radius * std::sqrt(distR(randomEngine)); // 面積一様分布
        float theta = distAngle(randomEngine);

        Vector3 offset = { r * std::cos(theta), 0.0f, r * std::sin(theta) };
        return { setting.position.x + offset.x, setting.position.y + offset.y, setting.position.z + offset.z };
    }
    case EmitShape::kCone:
        // コーンは発生位置は中心固定、広がりは速度方向側で表現する
        return setting.position;
    case EmitShape::kBox:
    default:
        return setting.position;
    }
}

Vector3 ParticleEmitter::GenerateVelocity() {
    switch (setting.shape) {
    case EmitShape::kSphere: {
        // 中心から外向きにランダムな方向＋ランダムな速さ
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * Calculation::kPi);
        std::uniform_real_distribution<float> distZ(-1.0f, 1.0f);
        std::uniform_real_distribution<float> distSpeed(setting.minSpeed, setting.maxSpeed);

        float theta = distAngle(randomEngine);
        float z = distZ(randomEngine);
        float xyScale = std::sqrt((std::max)(0.0f, 1.0f - z * z));
        float speed = distSpeed(randomEngine);

        return { speed * xyScale * std::cos(theta), speed * xyScale * std::sin(theta), speed * z };
    }
    case EmitShape::kCircle: {
        // XZ平面上で中心から外向き
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * Calculation::kPi);
        std::uniform_real_distribution<float> distSpeed(setting.minSpeed, setting.maxSpeed);

        float theta = distAngle(randomEngine);
        float speed = distSpeed(randomEngine);

        return { speed * std::cos(theta), 0.0f, speed * std::sin(theta) };
    }
    case EmitShape::kCone: {
        // setting.direction を中心軸として、coneAngleの範囲でランダムに広がる方向へ放出
        Vector3 axis = Calculation::Normalize(setting.direction);

        // 軸に垂直な基底ベクトルを2つ作る
        Vector3 arbitrary = (std::abs(axis.y) < 0.99f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
        Vector3 tangent = {
            arbitrary.y * axis.z - arbitrary.z * axis.y,
            arbitrary.z * axis.x - arbitrary.x * axis.z,
            arbitrary.x * axis.y - arbitrary.y * axis.x,
        };
        tangent = Calculation::Normalize(tangent);
        Vector3 bitangent = {
            axis.y * tangent.z - axis.z * tangent.y,
            axis.z * tangent.x - axis.x * tangent.z,
            axis.x * tangent.y - axis.y * tangent.x,
        };

        std::uniform_real_distribution<float> distCosAngle(std::cos(setting.coneAngle), 1.0f);
        std::uniform_real_distribution<float> distPhi(0.0f, 2.0f * Calculation::kPi);
        std::uniform_real_distribution<float> distSpeed(setting.minSpeed, setting.maxSpeed);

        float cosAngle = distCosAngle(randomEngine); // コーン角内で一様に分布させた軸とのなす角の余弦
        float sinAngle = std::sqrt((std::max)(0.0f, 1.0f - cosAngle * cosAngle));
        float phi = distPhi(randomEngine);
        float speed = distSpeed(randomEngine);

        Vector3 dir = {
            tangent.x * sinAngle * std::cos(phi) + bitangent.x * sinAngle * std::sin(phi) + axis.x * cosAngle,
            tangent.y * sinAngle * std::cos(phi) + bitangent.y * sinAngle * std::sin(phi) + axis.y * cosAngle,
            tangent.z * sinAngle * std::cos(phi) + bitangent.z * sinAngle * std::sin(phi) + axis.z * cosAngle,
        };

        return { dir.x * speed, dir.y * speed, dir.z * speed };
    }
    case EmitShape::kBox:
    default: {
        // 従来通り：min~maxVelocityの範囲でランダム（成分が同じ場合はその値を使用）
        Vector3 velocity;

        if (setting.minVelocity.x == setting.maxVelocity.x) {
            velocity.x = setting.minVelocity.x;
        } else {
            float minV = (std::min)(setting.minVelocity.x, setting.maxVelocity.x);
            float maxV = (std::max)(setting.minVelocity.x, setting.maxVelocity.x);
            std::uniform_real_distribution<float> dist(minV, maxV);
            velocity.x = dist(randomEngine);
        }

        if (setting.minVelocity.y == setting.maxVelocity.y) {
            velocity.y = setting.minVelocity.y;
        } else {
            float minV = (std::min)(setting.minVelocity.y, setting.maxVelocity.y);
            float maxV = (std::max)(setting.minVelocity.y, setting.maxVelocity.y);
            std::uniform_real_distribution<float> dist(minV, maxV);
            velocity.y = dist(randomEngine);
        }

        if (setting.minVelocity.z == setting.maxVelocity.z) {
            velocity.z = setting.minVelocity.z;
        } else {
            float minV = (std::min)(setting.minVelocity.z, setting.maxVelocity.z);
            float maxV = (std::max)(setting.minVelocity.z, setting.maxVelocity.z);
            std::uniform_real_distribution<float> dist(minV, maxV);
            velocity.z = dist(randomEngine);
        }

        return velocity;
    }
    }
}
