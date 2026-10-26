#include "SkinningTestScene.h"
#include "Framework.h"
#include "CameraManager.h"
#include "ModelManager.h"
#include "Logger.h"
#include "Input.h"
#include "GameTime.h"
#include "SpriteCommon.h"
#include <algorithm>
#include <cmath>
#include <format>

using namespace Logger;

std::vector<VertexData> SkinningTestScene::BuildFloorVertices() const {
    // 影を受けるための床（非スキニング。boneWeights/boneIndicesはVertexDataのデフォルト値={1,0,0,0}/{0,0,0,0}のまま
    // ＝常にボーン0(=Object3dCommonのデフォルト単位行列パレット)を参照するので、実質何も変形しない）
    constexpr float kFloorHalfSize = 6.0f;
    constexpr float kFloorY = -0.1f;

    Vector4 posBL = { -kFloorHalfSize, kFloorY, -kFloorHalfSize, 1.0f };
    Vector4 posBR = {  kFloorHalfSize, kFloorY, -kFloorHalfSize, 1.0f };
    Vector4 posTR = {  kFloorHalfSize, kFloorY,  kFloorHalfSize, 1.0f };
    Vector4 posTL = { -kFloorHalfSize, kFloorY,  kFloorHalfSize, 1.0f };
    Vector3 up = { 0.0f, 1.0f, 0.0f };

    std::vector<VertexData> vertices;
    vertices.push_back(VertexData{ posBL, { 0.0f, 1.0f }, up });
    vertices.push_back(VertexData{ posBR, { 1.0f, 1.0f }, up });
    vertices.push_back(VertexData{ posTR, { 1.0f, 0.0f }, up });

    vertices.push_back(VertexData{ posBL, { 0.0f, 1.0f }, up });
    vertices.push_back(VertexData{ posTR, { 1.0f, 0.0f }, up });
    vertices.push_back(VertexData{ posTL, { 0.0f, 0.0f }, up });

    return vertices;
}

void SkinningTestScene::Initialize() {
    Framework* framework = Framework::GetInstance();
    DirectXCommon* dxCommon = framework->GetDxCommon();
    Object3dCommon* object3dCommon = framework->GetObject3dCommon();

    // --- カメラ ---
    CameraManager::GetInstance()->Initialize();
    auto newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    CameraManager::GetInstance()->AddCamera("default", std::move(newCamera));
    ResetCamera();
    CameraManager::GetInstance()->SetActiveCamera("default");

    // --- glTFモデル（左から順に並べる） ---
    // 2ジョイントの棒（自動生成。joint1のRotationアニメーション1本、t=0→0.5→1.0でループ）
    AddGltf("棒", "Resources/gltf", "TestArm_embedded.gltf", { -4.0f, 0.0f, 0.0f }, 0.8f);
    // 人型（.glb・中に入っている jpg・matrix で書かれた Z上→Y上 のノード）
    AddGltf("CesiumMan", "Resources/gltf/khronos/CesiumMan", "CesiumMan.glb", { -2.0f, 0.0f, 0.0f }, 1.0f);
    // 人型（画像なし・マテリアルの色だけ。キーが2つだけの短い動き）
    AddGltf("RiggedFigure", "Resources/gltf/khronos/RiggedFigure", "RiggedFigure.glb", { 0.0f, 0.0f, 0.0f }, 1.0f);
    // キツネ（NORMAL なし・根元が複数・アニメーション3本）。元の単位がセンチなので小さくする
    fox = AddGltf("Fox", "Resources/gltf/khronos/Fox", "Fox.glb", { 2.0f, 0.0f, 0.0f }, 0.02f).skinned;
    SetupFoxAnimator();
    // 箱（.gltf + 外部の .bin と .png。骨もアニメーションも無い → ModelManager::LoadModel で読む）
    AddGltf("BoxTextured", "Resources/gltf/khronos/BoxTextured", "BoxTextured.gltf", { 4.0f, 0.5f, 0.0f }, 1.0f);

    // --- 奥の列 ---
    // トラック（メッシュ2つ・プリミティブ4つ・同じ車輪メッシュを2つのノードで使う。骨は無いが車輪のノードが回るアニメーションがある）
    AddGltf("CesiumMilkTruck", "Resources/gltf/khronos/CesiumMilkTruck", "CesiumMilkTruck.glb", { -2.5f, 0.0f, 5.0f }, 1.0f);
    // モーフターゲット2つを重みのアニメーションで混ぜる箱（ノードの大きさ 100 × 頂点 0.01 で 1辺 2）
    morphCube = AddGltf("MorphCube", "Resources/gltf/khronos/AnimatedMorphCube", "AnimatedMorphCube.glb", { 0.0f, 1.0f, 5.0f }, 0.5f).skinned;
    // 骨で動くモデルを ModelManager::LoadModel で動かさずに表示（レストポーズで立って止まる）
    AddStaticModel("CesiumMan（動かない）", "Resources/gltf/khronos/CesiumMan", "CesiumMan.glb", { 2.5f, 0.0f, 5.0f }, 1.0f);

    // キツネの頭に付いてくる黄色い箱
    cubeModel = ModelManager::GetInstance()->GetCubeModel();
    headMarker = std::make_unique<Object3d>();
    headMarker->Initialize(object3dCommon);
    headMarker->SetModel(cubeModel);
    headMarker->SetScale({ 0.15f, 0.15f, 0.15f });
    headMarker->SetColor({ 1.0f, 0.85f, 0.1f, 1.0f });

    // --- 文字 ---
    constexpr int kUiLayer = 100;
    SpriteCommon* spriteCommon = framework->GetSpriteCommon();
    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 20.0f);
    helpText->SetText("glTF テスト (SKINNING_TEST)\n"
                      "[Space] キツネのアニメーション切り替え  [L] ループ / 1回  [B] つなぐ秒数  [↑][↓] 速さ  [M] モーフの重みを自分で決める\n"
                      "[N] キツネの Walk と Run を混ぜる  [←][→] 混ぜる割合  [C] 状態遷移で動かす（[←][→] Speed  [T] Look）\n"
                      "カメラ: 右ドラッグで回す  ホイールで近づく  [WASD][QE] 見る点を動かす  [R] 戻す");
    helpText->SetPosition({ 20.0f, 16.0f });
    helpText->SetLayer(kUiLayer);
    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 20.0f);
    statusText->SetPosition({ 20.0f, 124.0f });
    statusText->SetLayer(kUiLayer);

    // --- 床(非スキニング。デフォルトパレットのフォールバック確認を兼ねる) ---
    floorModel = std::make_unique<Model>();
    floorModel->InitializeFromVertices(dxCommon, BuildFloorVertices(), "white");

    floorObject = std::make_unique<Object3d>();
    floorObject->Initialize(object3dCommon);
    floorObject->SetModel(floorModel.get());
    // SetBonePalette()は呼ばない → Object3dCommonのデフォルト単位行列パレットにフォールバックする
}

SkinningTestScene::GltfEntry& SkinningTestScene::AddGltf(const std::string& label, const std::string& directory, const std::string& file,
                                                         const Vector3& position, float scale) {
    Framework* framework = Framework::GetInstance();

    std::shared_ptr<GltfModelData> data = ModelManager::GetInstance()->LoadGltfModel(directory, file);
    if (data->skin.jointNodeIndices.empty() && data->animations.empty()) {
        // 骨もアニメーションも無ければ動かないので、普通のモデルとして読む
        return AddStaticModel(label, directory, file, position, scale);
    }

    // 表示用の Model は SkinnedObject3d が中で作る（プリミティブごとにマテリアルの画像を読み込む）
    auto skinned = std::make_unique<SkinnedObject3d>();
    skinned->Initialize(framework->GetObject3dCommon());
    skinned->SetGltfData(framework->GetDxCommon(), data);

    GltfEntry entry;
    entry.label = label;
    entry.skinned = skinned.get();
    entry.object = std::move(skinned);
    entry.object->SetTranslate(position);
    entry.object->SetScale({ scale, scale, scale });
    entries.push_back(std::move(entry));
    return entries.back();
}

SkinningTestScene::GltfEntry& SkinningTestScene::AddStaticModel(const std::string& label, const std::string& directory, const std::string& file,
                                                                const Vector3& position, float scale) {
    GltfEntry entry;
    entry.label = label;
    entry.model = ModelManager::GetInstance()->LoadModel(directory, file);
    entry.object = std::make_unique<Object3d>();
    entry.object->Initialize(Framework::GetInstance()->GetObject3dCommon());
    entry.object->SetModel(entry.model.get());
    entry.object->SetTranslate(position);
    entry.object->SetScale({ scale, scale, scale });
    entries.push_back(std::move(entry));
    return entries.back();
}

void SkinningTestScene::Finalize() {
    CameraManager::GetInstance()->Initialize();
    camera = nullptr;
    fox = nullptr;
    // 手放すだけ（誰も持っていなければ、シーン切り替えの後に ModelManager が解放する）
    entries.clear();
}

void SkinningTestScene::SetupFoxAnimator() {
    // Idle（見回す）⇔ Move（Speed 0.3〜1.0 で Walk → Run を混ぜる）、どこからでも [T] で Look（1回見回して Idle へ）
    constexpr float kMoveThreshold = 0.05f;
    foxAnimator.Initialize(fox);
    foxAnimator.AddState("Idle", { .animation = "Survey" });
    foxAnimator.AddState("Move", { .animation = "Walk", .blendAnimation = "Run",
                                   .blendParameter = "Speed", .blendParameterMin = 0.3f, .blendParameterMax = 1.0f });
    foxAnimator.AddState("Look", { .animation = "Survey", .loop = false });
    foxAnimator.AddTransition({ .from = "Idle", .to = "Move", .blendSeconds = 0.3f,
                                .condition = [](const AnimationStateMachine& a) { return a.GetFloat("Speed") > kMoveThreshold; } });
    foxAnimator.AddTransition({ .from = "Move", .to = "Idle", .blendSeconds = 0.4f,
                                .condition = [](const AnimationStateMachine& a) { return a.GetFloat("Speed") <= kMoveThreshold; } });
    foxAnimator.AddTransition({ .from = "*", .to = "Look", .blendSeconds = 0.2f, .trigger = "Look" });
    foxAnimator.AddTransition({ .from = "Look", .to = "Idle", .blendSeconds = 0.3f, .requireFinished = true });
    foxAnimator.SetOnStateChanged([this](const std::string& from, const std::string& to) {
        lastStateChange = (from.empty() ? "(開始)" : from) + " → " + to;
    });
}

void SkinningTestScene::UpdateFoxDirectControl() {
    Input* input = Framework::GetInstance()->GetInput();
    const size_t count = fox->GetAnimationCount();
    const int32_t current = fox->GetCurrentAnimationIndex();
    // [Space] 次のアニメーションへ（今のループ設定のまま最初から）
    if (input->TriggerKey(DIK_SPACE)) {
        const size_t next = current < 0 ? 0 : (static_cast<size_t>(current) + 1) % count;
        fox->PlayAnimation(next, fox->IsLooping(), foxBlendSeconds);
    }
    // [L] ループ ↔ 1回 を切り替えて最初から
    if (input->TriggerKey(DIK_L)) {
        fox->PlayAnimation(current < 0 ? 0 : static_cast<size_t>(current), !fox->IsLooping(), foxBlendSeconds);
    }
    // [B] つなぐ秒数を 0（すぐ切り替え）→ 0.3 → 1.0 → 0 …
    if (input->TriggerKey(DIK_B)) {
        foxBlendSeconds = foxBlendSeconds == 0.0f ? 0.3f : (foxBlendSeconds < 1.0f ? 1.0f : 0.0f);
    }
    // [N] Walk と Run を混ぜる ↔ 1本ずつ再生
    if (input->TriggerKey(DIK_N)) {
        if (fox->IsPlayingBlend()) {
            fox->PlayAnimation("Walk", true, foxBlendSeconds);
        } else {
            fox->PlayBlend("Walk", "Run", foxBlendRate, foxBlendSeconds);
        }
    }
    // [←][→] 混ぜる割合（0 = Walk、1 = Run）
    constexpr float kRateStep = 0.1f;
    if (input->TriggerKey(DIK_LEFT)) { foxBlendRate = (std::max)(foxBlendRate - kRateStep, 0.0f); }
    if (input->TriggerKey(DIK_RIGHT)) { foxBlendRate = (std::min)(foxBlendRate + kRateStep, 1.0f); }
    fox->SetBlendRate(foxBlendRate);
}

void SkinningTestScene::UpdateFoxStateMachineControl() {
    Input* input = Framework::GetInstance()->GetInput();
    // [←][→] Speed パラメーター（ゲームならキャラクターの移動の速さを入れる）
    constexpr float kSpeedStep = 0.1f;
    if (input->TriggerKey(DIK_LEFT)) { foxSpeedParameter = (std::max)(foxSpeedParameter - kSpeedStep, 0.0f); }
    if (input->TriggerKey(DIK_RIGHT)) { foxSpeedParameter = (std::min)(foxSpeedParameter + kSpeedStep, 1.0f); }
    foxAnimator.SetFloat("Speed", foxSpeedParameter);
    // [T] Look トリガー（押した瞬間だけ）
    if (input->TriggerKey(DIK_T)) {
        foxAnimator.SetTrigger("Look");
    }
    foxAnimator.Update();
}

void SkinningTestScene::UpdateAnimationControl() {
    constexpr float kSpeedStep = 0.25f;
    constexpr float kMaxSpeed = 3.0f;
    Input* input = Framework::GetInstance()->GetInput();

    if (fox && fox->GetAnimationCount() > 0) {
        // [C] 直接操作 ↔ 状態遷移で動かす
        if (input->TriggerKey(DIK_C)) {
            isFoxStateMachineMode = !isFoxStateMachineMode;
            if (isFoxStateMachineMode) {
                foxSpeedParameter = 0.0f;
                foxAnimator.ChangeState("Idle", 0.3f);
            } else {
                fox->PlayAnimation(size_t{ 0 }, true, 0.3f);
            }
        }
        if (isFoxStateMachineMode) {
            UpdateFoxStateMachineControl();
        } else {
            UpdateFoxDirectControl();
        }
    }

    // [M] アニメーションのまま → ターゲット0だけ 1 → ターゲット1だけ 1 → … → アニメーションのまま
    if (morphCube && morphCube->GetMorphTargetCount() > 0 && input->TriggerKey(DIK_M)) {
        morphOverrideMode = (morphOverrideMode + 1) % (morphCube->GetMorphTargetCount() + 1);
        morphCube->ClearMorphWeights();
        if (morphOverrideMode > 0) {
            for (size_t i = 0; i < morphCube->GetMorphTargetCount(); ++i) {
                morphCube->SetMorphWeight(i, i + 1 == morphOverrideMode ? 1.0f : 0.0f);
            }
        }
    }

    // [↑][↓] 動く物すべての速さ
    if (input->TriggerKey(DIK_UP)) { animationSpeed = (std::min)(animationSpeed + kSpeedStep, kMaxSpeed); }
    if (input->TriggerKey(DIK_DOWN)) { animationSpeed = (std::max)(animationSpeed - kSpeedStep, 0.0f); }
    for (GltfEntry& entry : entries) {
        if (entry.skinned) {
            entry.skinned->SetAnimationSpeed(animationSpeed);
        }
    }
}

void SkinningTestScene::UpdateHeadMarker() {
    Matrix4x4 headMatrix{};
    if (!fox || !fox->GetJointWorldMatrix("b_Head_05", headMatrix)) {
        return;
    }
    // 行列の平行移動の部分が頭の骨の位置。少し上に浮かせる
    constexpr float kOffsetY = 0.35f;
    headMarker->SetTranslate({ headMatrix.m[3][0], headMatrix.m[3][1] + kOffsetY, headMatrix.m[3][2] });
}

std::string SkinningTestScene::BuildStatusText() const {
    std::string text = std::format("速さ {:.2f}  キツネ: {}  つなぐ {:.1f} 秒{}", animationSpeed,
                                   fox && fox->IsLooping() ? "ループ" : "1回", foxBlendSeconds,
                                   fox && fox->IsBlending() ? "（つないでいる途中）" : "");
    if (isFoxStateMachineMode) {
        text += std::format("\n状態遷移: 状態 {}（{:.1f} 秒）  Speed {:.1f}  最後の切り替え {}", foxAnimator.GetCurrentState(),
                            foxAnimator.GetStateTime(), foxSpeedParameter, lastStateChange);
    }
    for (const GltfEntry& entry : entries) {
        if (!entry.skinned) {
            continue;
        }
        const SkinnedObject3d& object = *entry.skinned;
        const int32_t index = object.GetCurrentAnimationIndex();
        if (object.IsPlayingBlend()) {
            text += std::format("\n{}: {} と {} を混ぜている  割合 {:.1f}  1周のうち {:.2f}", entry.label,
                                object.GetAnimationName(static_cast<size_t>(index)),
                                object.GetAnimationName(static_cast<size_t>(object.GetBlendSecondIndex())),
                                object.GetBlendRate(), object.GetAnimationTime());
            continue;
        }
        if (index < 0) {
            text += std::format("\n{}: アニメーションなし", entry.label);
            continue;
        }
        text += std::format("\n{}: {} ({}/{})  {:.2f} / {:.2f} 秒{}", entry.label,
                            object.GetAnimationName(static_cast<size_t>(index)), index + 1, object.GetAnimationCount(),
                            object.GetAnimationTime(), object.GetAnimationDuration(static_cast<size_t>(index)),
                            object.IsAnimationFinished() ? "  (終わり)" : "");
    }
    if (morphCube) {
        text += std::format("\nモーフの重み ({}):", morphOverrideMode == 0 ? "アニメーション" : "自分で決めた値");
        for (size_t i = 0; i < morphCube->GetMorphTargetCount(); ++i) {
            const std::string& name = morphCube->GetMorphTargetName(i);
            text += std::format("  {} {:.2f}", name.empty() ? std::to_string(i) : name, morphCube->GetMorphWeight(i));
        }
    }
    return text;
}

void SkinningTestScene::ResetCamera() {
    // 手前の列と奥の列が両方見える位置から、少し見下ろす
    orbitTarget = { 0.0f, 1.0f, 2.0f };
    orbitYaw = 0.0f;
    orbitPitch = 0.15f;
    orbitDistance = 10.5f;
}

void SkinningTestScene::UpdateCameraControl() {
    constexpr float kRotateSpeed = 0.005f;   // 1ピクセルあたりの回転（ラジアン）
    constexpr float kZoomSpeed = 0.005f;     // ホイール1目盛り(120)あたり約0.6
    constexpr float kMoveSpeed = 4.0f;       // 見る点の移動（1秒あたり）
    constexpr float kMinDistance = 0.5f;
    constexpr float kMaxDistance = 40.0f;
    constexpr float kMaxPitch = 1.5f;        // 真上・真下の手前で止める

    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_R)) {
        ResetCamera();
    }
    // 右ボタン(1)を押しながらドラッグで回す
    if (input->PushMouseButton(1)) {
        Input::MouseMove move = input->GetMouseMove();
        orbitYaw += move.lX * kRotateSpeed;
        orbitPitch = std::clamp(orbitPitch + move.lY * kRotateSpeed, -kMaxPitch, kMaxPitch);
    }
    orbitDistance = std::clamp(orbitDistance - input->GetMouseWheel() * kZoomSpeed, kMinDistance, kMaxDistance);

    // WASD は今の向きのまま水平に、QE は上下に見る点を動かす
    const float step = kMoveSpeed * Time::GetDeltaTime();
    const Vector3 flatForward = { std::sin(orbitYaw), 0.0f, std::cos(orbitYaw) };
    const Vector3 flatRight = { std::cos(orbitYaw), 0.0f, -std::sin(orbitYaw) };
    if (input->PushKey(DIK_W)) { orbitTarget += flatForward * step; }
    if (input->PushKey(DIK_S)) { orbitTarget -= flatForward * step; }
    if (input->PushKey(DIK_D)) { orbitTarget += flatRight * step; }
    if (input->PushKey(DIK_A)) { orbitTarget -= flatRight * step; }
    if (input->PushKey(DIK_E)) { orbitTarget.y += step; }
    if (input->PushKey(DIK_Q)) { orbitTarget.y -= step; }

    // Camera の回転（x=見下ろす角度、y=左右の向き）で前を向く方向。カメラは見る点から後ろへ距離だけ下がった所
    const Vector3 forward = {
        std::cos(orbitPitch) * std::sin(orbitYaw), -std::sin(orbitPitch), std::cos(orbitPitch) * std::cos(orbitYaw) };
    camera->SetTranslate(orbitTarget - forward * orbitDistance);
    camera->SetRotate({ orbitPitch, orbitYaw, 0.0f });
}

void SkinningTestScene::Update() {
    if (camera) {
        UpdateCameraControl();
        camera->Update();
    }

    UpdateAnimationControl();
    for (GltfEntry& entry : entries) {
        entry.object->Update();
    }
    // キツネの Update（骨の計算）の後で頭の位置を読む
    UpdateHeadMarker();
    headMarker->Update();
    floorObject->Update();

    statusText->SetText(BuildStatusText());
    helpText->Update();
    statusText->Update();
}

void SkinningTestScene::Draw() {
    Framework* framework = Framework::GetInstance();
    DirectXCommon* dxCommon = framework->GetDxCommon();
    Object3dCommon* object3dCommon = framework->GetObject3dCommon();

    // --- シャドウパス（ライト視点の深度のみ描画） ---
    object3dCommon->PreDrawShadow();
    Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    for (GltfEntry& entry : entries) {
        entry.object->DrawShadow(lightViewProjection);
    }
    headMarker->DrawShadow(lightViewProjection);
    floorObject->DrawShadow(lightViewProjection);
    object3dCommon->PostDrawShadow();

    // PostDrawShadow()はビューポート/シザーのみ復元するため、メインのレンダーターゲット(RTV/DSV)は
    // ここで明示的に張り直す必要がある（内部解像度を下げているときは小さい描画先になるので、バックバッファを直接指定しない）
    dxCommon->BindMainRenderTarget();

    // --- 通常の描画パス ---
    object3dCommon->PreDraw();
    for (GltfEntry& entry : entries) {
        entry.object->Draw();
    }
    headMarker->Draw();
    floorObject->Draw();

    helpText->Draw();
    statusText->Draw();
}
