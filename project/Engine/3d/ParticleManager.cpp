#include "ParticleManager.h"
#include "CameraManager.h"
#include "Framework.h"
#include <random>
#include <algorithm>
#include <cmath>

void ParticleManager::Initialize(ParticleCommon* common, uint32_t textureIndex, ParticleDrawType drawType) {
    this->common = common;
    this->textureIndex = textureIndex;
    this->drawType = drawType;

    // 1. インスタンシング用リソースの作成 (StructuredBuffer)
    instancingResource = common->GetDxCommon()->CreatBufferResource(sizeof(ParticleForGPU) * kMaxParticles);
    instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

    // 2. 頂点リソースの作成 (最大4頂点の領域を確保)
    vertexResource = common->GetDxCommon()->CreatBufferResource(sizeof(VertexData) * 4);
    VertexData* vertexData = nullptr;
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    uint32_t vertexCount = 1;
    if (drawType == ParticleDrawType::kPoint) {
        vertexData[0] = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f}, {0.0f, 0.0f, -1.0f} };
        vertexCount = 1;
    } else if (drawType == ParticleDrawType::kLine) {
        // 線（とりあえずY軸方向に少し伸びる線）
        vertexData[0] = { {0.0f, -0.5f, 0.0f, 1.0f}, {0.5f, 0.0f}, {0.0f, 0.0f, -1.0f} };
        vertexData[1] = { {0.0f,  0.5f, 0.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 0.0f, -1.0f} };
        vertexCount = 2;
    } else {
        // 四角形（面）
        vertexData[0] = { {-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f} }; // 左下
        vertexData[1] = { {-0.5f,  0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} }; // 左上
        vertexData[2] = { { 0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f} }; // 右下
        vertexData[3] = { { 0.5f,  0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f} }; // 右上
        vertexCount = 4;
    }

    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof(VertexData) * vertexCount;
    vertexBufferView.StrideInBytes = sizeof(VertexData);
}

void ParticleManager::Emit(const ParticleEmitParams& params) {
    // 最大数を超えていたら追加しない
    if (particles.size() >= kMaxParticles) {
        return;
    }

    Particle p;
    p.transform.translate = params.position;
    p.transform.rotate = { 0, 0, 0 };
    p.transform.scale = params.startScale;
    p.velocity = params.velocity;
    p.color = params.color;
    p.lifeTime = params.lifeTime;
    p.currentTime = 0;

    p.startScale = params.startScale;
    p.endScale = params.endScale;
    p.startColor = params.startColor;
    p.endColor = params.endColor;
    p.useColorGradient = params.useColorGradient;

    p.spriteSheetColumns = params.spriteSheetColumns > 0 ? params.spriteSheetColumns : 1;
    p.spriteSheetRows = params.spriteSheetRows > 0 ? params.spriteSheetRows : 1;
    p.loopSpriteAnimation = params.loopSpriteAnimation;

    // 物理パラメータ（重力・空気抵抗）。Update()で毎フレーム速度に適用する
    p.gravity = params.gravity;
    p.drag = params.drag;

    // 線専用パラメータ。長さは固定値ではなく、Update()で毎フレームその時点の速さから再計算する
    p.lineWidth = params.lineWidth;
    p.lineLengthMultiplier = params.lineLengthMultiplier;
    p.maxLineLength = params.maxLineLength;

    p.roll = params.roll;
    p.shear = params.shear;

    particles.push_back(p);
}

void ParticleManager::Emit(const Vector3& position, const Vector3& velocity, const Vector4& color, float lifeTime) {
    ParticleEmitParams params;
    params.position = position;
    params.velocity = velocity;
    params.color = color;
    params.lifeTime = lifeTime;
    // 互換用は従来通りcolorのアルファフェードのみ（useColorGradient=false）
    Emit(params);
}

void ParticleManager::Update() {
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    Matrix4x4 viewProjectionMatrix = camera ? camera->GetViewProjectionMatrix() : Calculation::MakeIdentity4x4();
    Matrix4x4 viewMatrix = camera ? camera->GetViewMatrix() : Calculation::MakeIdentity4x4();

    instanceCount = 0;
    for (auto it = particles.begin(); it != particles.end(); ) {
        // 寿命チェック
        it->currentTime += Calculation::kFixedDeltaTime; // 暫定固定フレームレート前提（本来は実フレーム時間を受け取るべき）
        if (it->currentTime >= it->lifeTime) {
            it = particles.erase(it);
            continue;
        }

        // 空気抵抗（毎フレーム速度を減衰させる。値が大きいほど急激に減速する）
        if (it->drag > 0.0f) {
            float dragFactor = (std::max)(0.0f, 1.0f - it->drag);
            it->velocity.x *= dragFactor;
            it->velocity.y *= dragFactor;
            it->velocity.z *= dragFactor;
        }

        // 重力（毎フレーム速度に加算）
        it->velocity.x += it->gravity.x;
        it->velocity.y += it->gravity.y;
        it->velocity.z += it->gravity.z;

        // 移動
        it->transform.translate.x += it->velocity.x;
        it->transform.translate.y += it->velocity.y;
        it->transform.translate.z += it->velocity.z;

        // 経過割合 (0.0〜1.0)
        float t = it->currentTime / it->lifeTime;
        t = (std::max)(0.0f, (std::min)(1.0f, t));

        // スケールの時間補間（startScale → endScale）
        Vector3 currentScale = {
            it->startScale.x + (it->endScale.x - it->startScale.x) * t,
            it->startScale.y + (it->endScale.y - it->startScale.y) * t,
            it->startScale.z + (it->endScale.z - it->startScale.z) * t,
        };

        if (drawType == ParticleDrawType::kLine) {
            // 線タイプ：長さは「その時点の速さ」から毎フレーム再計算する。
            // 重力や空気抵抗で速度が変化すれば、それに追従して長さも伸び縮みする。
            // 太さ(X/Z)は時間補間したcurrentScaleにlineWidthを掛けたものを使う。
            float currentSpeed = Calculation::Length(it->velocity);
            float length = currentSpeed * it->lineLengthMultiplier;
            length = (std::min)(length, it->maxLineLength); // 速すぎて伸びすぎるのを防ぐ
            currentScale = {
                currentScale.x * it->lineWidth,
                length,
                currentScale.z * it->lineWidth,
            };
        }

        it->transform.scale = currentScale;

        // ワールド行列の作成
        Matrix4x4 worldMatrix;
        if (drawType == ParticleDrawType::kLine) {
            // 線タイプ: 速度ベクトル方向に線を向ける
            // 頂点バッファはY軸方向（{0,-0.5,0}～{0,+0.5,0}）に線を定義しているため、
            // Y+方向を速度ベクトル方向へ向けるクォータニオンを計算する
            float speed = Calculation::Length(it->velocity);
            Matrix4x4 rotMatrix;
            if (speed > 0.0001f) {
                // デフォルトのY+軸ベクトル
                Vector3 defaultUp = { 0.0f, 1.0f, 0.0f };
                // 正規化した速度ベクトルを目標方向とする
                Vector3 targetDir = Calculation::Normalize(it->velocity);
                // 2つのベクトルの外積から回転軸を求める
                // 外積 = defaultUp x targetDir
                Vector3 axis = {
                    defaultUp.y * targetDir.z - defaultUp.z * targetDir.y,
                    defaultUp.z * targetDir.x - defaultUp.x * targetDir.z,
                    defaultUp.x * targetDir.y - defaultUp.y * targetDir.x
                };
                float axisLen = Calculation::Length(axis);
                if (axisLen > 0.0001f) {
                    // 回転軸を正規化し、回転角度（内積からcos）を求める
                    axis = Calculation::Normalize(axis);
                    float cosAngle = Calculation::Dot(defaultUp, targetDir);
                    // clampしてacosの安全な入力範囲に収める（Windowsマクロとの競合を避けるため括弧でくくる）
                    cosAngle = (std::max)(-1.0f, (std::min)(1.0f, cosAngle));
                    float angle = std::acos(cosAngle);
                    Quaternion q = Calculation::MakeAxisAngleQuaternion(axis, angle);
                    rotMatrix = Calculation::MakeRotateMatrix(q);
                } else {
                    // defaultUpとtargetDirがほぼ同じ（または正反対）の場合
                    if (Calculation::Dot(defaultUp, targetDir) < 0.0f) {
                        // 正反対（速度がY-方向）: Z軸で180度回転
                        Quaternion q = Calculation::MakeAxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, Calculation::kPi);
                        rotMatrix = Calculation::MakeRotateMatrix(q);
                    } else {
                        // 同じ方向: 単位行列
                        rotMatrix = Calculation::MakeIdentity4x4();
                    }
                }
            } else {
                rotMatrix = Calculation::MakeIdentity4x4();
            }
            // スケール → 回転 → 平行移動 の順に合成
            Matrix4x4 scaleMatrix = Calculation::MakeScaleMatrix(it->transform.scale);
            Matrix4x4 translateMatrix = Calculation::MakeTranslationMatrix(it->transform.translate);
            worldMatrix = scaleMatrix * rotMatrix * translateMatrix;
        } else {
            // 点・面タイプ: ビルボード行列を使用。
            // roll(面内回転)とshear(平行四辺形変形)はビルボードの向きが決まる前のローカル空間で適用してから、
            // 「カメラに正対させる回転＋平行移動だけ」のビルボード行列を後段に掛ける。
            // こうすると回転・歪みがそのままスクリーン上の見た目に反映される。
            if (it->roll != 0.0f || it->shear != 0.0f) {
                Matrix4x4 scaleMatrix = Calculation::MakeScaleMatrix(it->transform.scale);

                // shear: Y成分に応じてXをずらして平行四辺形にする
                // ※ Matrix4x4 が float m[4][4] の行列であることを前提にしています。
                //    もしコンパイルエラーになる場合は、お使いのMatrix4x4の実装に合わせて
                //    「Y方向の値に比例してXをずらす」行列の作り方を教えてください。
                Matrix4x4 shearMatrix = Calculation::MakeIdentity4x4();
                shearMatrix.m[1][0] = it->shear;

                Matrix4x4 rollMatrix = Calculation::MakeRotationZMatrix(it->roll);

                // ビルボードの「回転＋平行移動」だけを取り出すため、スケールは1,1,1で渡す
                Matrix4x4 billboardRotTranslate = Calculation::MakeBillboardMatrix({ 1.0f, 1.0f, 1.0f }, it->transform.translate, viewMatrix);

                // スケール → せん断 → 面内回転 → ビルボード(回転+平行移動) の順に合成
                worldMatrix = scaleMatrix * shearMatrix * rollMatrix * billboardRotTranslate;
            } else {
                // roll/shearを使わない場合は従来通りのシンプルな計算
                worldMatrix = Calculation::MakeBillboardMatrix(it->transform.scale, it->transform.translate, viewMatrix);
            }
        }
        
        // GPU用データに書き込み
        if (instanceCount < kMaxParticles) {
            instancingData[instanceCount].World = worldMatrix;
            instancingData[instanceCount].WVP = worldMatrix * viewProjectionMatrix;

            // 色の時間変化
            if (it->useColorGradient) {
                Vector4 currentColor = {
                    it->startColor.x + (it->endColor.x - it->startColor.x) * t,
                    it->startColor.y + (it->endColor.y - it->startColor.y) * t,
                    it->startColor.z + (it->endColor.z - it->startColor.z) * t,
                    it->startColor.w + (it->endColor.w - it->startColor.w) * t,
                };
                instancingData[instanceCount].color = currentColor;
            } else {
                // 互換動作：colorのアルファのみ徐々にフェードアウト
                float alpha = 1.0f - t;
                instancingData[instanceCount].color = it->color;
                instancingData[instanceCount].color.w *= alpha;
            }

            // スプライトシートアニメーションのUV計算
            uint32_t totalFrames = it->spriteSheetColumns * it->spriteSheetRows;
            if (totalFrames <= 1) {
                instancingData[instanceCount].uvOffset = { 0.0f, 0.0f };
                instancingData[instanceCount].uvScale = { 1.0f, 1.0f };
            } else {
                // animTは0〜1の経過割合。寿命中にスプライトシート全体を1周するのが基本仕様。
                // 複数周ループさせたい場合はloopSpriteAnimationをtrueにし、寿命を短く設定して
                // Emitterのfrequencyで連続発生させることで疑似的にループ表現する。
                float animT = t;
                uint32_t frameIndex = (std::min)(totalFrames - 1, static_cast<uint32_t>(animT * static_cast<float>(totalFrames)));
                float uvW = 1.0f / static_cast<float>(it->spriteSheetColumns);
                float uvH = 1.0f / static_cast<float>(it->spriteSheetRows);
                uint32_t col = frameIndex % it->spriteSheetColumns;
                uint32_t row = frameIndex / it->spriteSheetColumns;
                instancingData[instanceCount].uvOffset = { uvW * static_cast<float>(col), uvH * static_cast<float>(row) };
                instancingData[instanceCount].uvScale = { uvW, uvH };
            }

            instanceCount++;
        }

        ++it;
    }
}

void ParticleManager::Draw() {
    if (instanceCount == 0) return;

    auto commandList = common->GetDxCommon()->GetCommandList();

    // VBVをセット
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

    // StructuredBuffer (t0) をセット
    commandList->SetGraphicsRootShaderResourceView(0, instancingResource->GetGPUVirtualAddress());

    // テクスチャ (t1) をセット (Framework経由でSrvManagerを取得)
    SrvManager* srvManager = Framework::GetInstance()->GetSrvManager();
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = srvManager->GetGPUDescriptorHandle(textureIndex);
    commandList->SetGraphicsRootDescriptorTable(1, textureHandle);

    // 描画タイプに応じた頂点数でインスタンシング描画
    uint32_t vertexCount = 1;
    if (drawType == ParticleDrawType::kLine) {
        vertexCount = 2;
    } else if (drawType == ParticleDrawType::kTriangle) {
        vertexCount = 4;
    }
    commandList->DrawInstanced(vertexCount, instanceCount, 0, 0);
}   