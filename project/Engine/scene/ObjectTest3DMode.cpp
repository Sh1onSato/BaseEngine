#include "ObjectTest3DMode.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameObjectManager.h"
#include "Model.h"
#include "Object3d.h"
#include <format>

namespace {
    // CameraManager に登録する名前（同じ名前の2回登録は assert になるので、Initialize で一度空にしてから登録する）
    const char* kCameraName = "ObjectTest3D";
    const Vector3 kCameraPosition = { 0.0f, 14.0f, -16.0f };
    const Vector3 kCameraRotate = { 0.75f, 0.0f, 0.0f }; // 斜めに見下ろす

    constexpr float kRayMaxDistance = 10.0f;
    // レイが当たる相手: プレイヤー自身とトリガー以外
    constexpr uint32_t kRayLayerMask = ~((1u << ObjectTest::kPlayer3DLayer) | (1u << ObjectTest::kTrigger3DLayer));
    constexpr float kHitMarkerSize = 0.25f;

    const Vector2 kPlayerStart = { 0.0f, -5.0f };
    const Vector4 kFloorColor = { 0.55f, 0.55f, 0.6f, 1.0f };
    const Vector4 kBoxColor = { 0.8f, 0.5f, 0.3f, 1.0f };
    const Vector4 kOrientedBoxColor = { 0.8f, 0.3f, 0.6f, 1.0f };
    const Vector4 kSphereColor = { 0.3f, 0.8f, 0.9f, 1.0f };
    const Vector4 kHitMarkerColor = { 1.0f, 0.9f, 0.1f, 1.0f };
}

namespace ObjectTest {

Collision3DTest::Collision3DTest() = default;
// Object3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く
Collision3DTest::~Collision3DTest() = default;

void Collision3DTest::Initialize() {
    SetupCamera();
    meshes.Initialize();

    hitMarker = std::make_unique<Object3d>();
    hitMarker->Initialize(Framework::GetInstance()->GetObject3dCommon());
    hitMarker->SetModel(meshes.GetCube());
    hitMarker->SetScale({ kHitMarkerSize, kHitMarkerSize, kHitMarkerSize });
    hitMarker->SetColor(kHitMarkerColor);
}

void Collision3DTest::SetupCamera() {
    // SkinningTestScene と同じやり方: 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate(kCameraPosition);
    camera->SetRotate(kCameraRotate);
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void Collision3DTest::Spawn(GameObjectManager& objects) {
    triggerEntryCount = 0;
    hasRayHit = false;
    Model* cube = meshes.GetCube();

    // 床（上面が y = 0）
    objects.Create<Obstacle>(cube, "Floor", Vector3{ 0.0f, -0.5f, 0.0f }, Vector3{ 24.0f, 1.0f, 24.0f },
                             0.0f, kFloorColor, Collider::MakeBox({ 12.0f, 0.5f, 12.0f }));
    // 回転しない箱（AABB）
    objects.Create<Obstacle>(cube, "Box (AABB)", Vector3{ -4.0f, 1.0f, 3.0f }, Vector3{ 2.0f, 2.0f, 2.0f },
                             0.0f, kBoxColor, Collider::MakeBox({ 1.0f, 1.0f, 1.0f }));
    // Y軸まわりに約35度回した箱（OBB）
    objects.Create<Obstacle>(cube, "Box (OBB)", Vector3{ 4.0f, 1.0f, 3.0f }, Vector3{ 3.0f, 2.0f, 1.5f },
                             0.6f, kOrientedBoxColor, Collider::MakeOrientedBox({ 1.5f, 1.0f, 0.75f }));
    // 大きな球（直径3。メッシュの半径0.5を scale 3 で広げる）
    objects.Create<Obstacle>(meshes.GetSphere(), "Sphere", Vector3{ 0.0f, 1.5f, 6.0f }, Vector3{ 3.0f, 3.0f, 3.0f },
                             0.0f, kSphereColor, Collider::MakeSphere(1.5f));

    objects.Create<Crate3D>(cube, Vector2{ -2.0f, -2.0f });
    objects.Create<TriggerZone>(cube, Vector2{ 4.0f, -3.0f }, &triggerEntryCount);
    objects.Create<Player3D>(meshes, kPlayerStart, isSphereMode);
}

void Collision3DTest::TogglePlayerShape(GameObjectManager& objects) {
    isSphereMode = !isSphereMode;
    Vector2 positionXZ = kPlayerStart;
    if (Player3D* oldPlayer = objects.FindByType<Player3D>()) {
        positionXZ = { oldPlayer->transform.translate.x, oldPlayer->transform.translate.z };
        // 古い方はフレームの最後に消える（Destroy 済みの物は当たり判定・検索の対象外）
        oldPlayer->Destroy();
    }
    objects.Create<Player3D>(meshes, positionXZ, isSphereMode);
}

void Collision3DTest::Update(GameObjectManager& objects) {
    if (camera) {
        camera->Update(); // 画面の縦横比が変わっても追従するよう毎フレーム計算する
    }
    UpdateRaycast(objects);
}

void Collision3DTest::UpdateRaycast(GameObjectManager& objects) {
    hasRayHit = false;
    const Player3D* player = objects.FindByType<Player3D>();
    if (!player) {
        return;
    }
    // プレイヤーの中心から、最後に動いた向きへ飛ばす（対象は 球・AABB・OBB のみ）
    Ray ray{ player->transform.translate, player->GetFacing() };
    RaycastHit hit{};
    Collider* hitCollider = nullptr;
    if (!objects.GetCollisionManager().Raycast(ray, kRayMaxDistance, kRayLayerMask, &hit, &hitCollider)) {
        return;
    }
    hasRayHit = true;
    rayHitDistance = hit.distance;
    rayHitPoint = hit.hitPoint;
    GameObject* owner = hitCollider ? hitCollider->GetOwner() : nullptr;
    rayHitName = owner ? owner->GetName() : "(unknown)";
}

void Collision3DTest::Draw(GameObjectManager& objects) {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();

    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass(objects);
    RebindMainRenderTarget();

    // --- 通常の描画パス ---
    object3dCommon->PreDraw();
    objects.Draw(); // MeshObject::Draw が Object3d を描く
    if (hasRayHit) {
        hitMarker->SetTranslate(rayHitPoint);
        hitMarker->Update();
        hitMarker->Draw();
    }
}

void Collision3DTest::DrawShadowPass(GameObjectManager& objects) {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    for (MeshObject* mesh : objects.FindAllByType<MeshObject>()) {
        if (mesh->IsActive()) {
            mesh->DrawShadow(lightViewProjection);
        }
    }
    object3dCommon->PostDrawShadow();
}

void Collision3DTest::RebindMainRenderTarget() {
    // PostDrawShadow() はビューポート/シザーしか戻さないので、描画先（RTV/DSV）はここで張り直す
    // （SkinningTestScene::Draw と同じ手順）
    DirectXCommon* dxCommon = Framework::GetInstance()->GetDxCommon();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetRtvHandle(dxCommon->GetBackBufferIndex());
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    dxCommon->GetCommandList()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
}

std::string Collision3DTest::BuildStatusText(const GameObjectManager& objects) const {
    const CollisionManager& collision = objects.GetCollisionManager();
    const Player3D* player = objects.FindByType<Player3D>();
    std::string rayText = hasRayHit ? std::format("{} ({:.2f})", rayHitName, rayHitDistance) : "なし";
    return std::format(
        "プレイヤーの形: {}\n"
        "プレイヤーの接触数: {}\n"
        "トリガーに入った回数: {}\n"
        "レイ: {}\n"
        "コライダー数: {}\n"
        "大まかな判定の組: {}\n"
        "当たっている組: {}",
        isSphereMode ? "球 (半径0.75)" : "カプセル (OBBとは未対応)",
        player ? player->GetContactCount() : 0,
        triggerEntryCount,
        rayText,
        collision.GetLastColliderCount(),
        collision.GetLastBroadphasePairCount(),
        collision.GetLastHitPairCount());
}

const char* Collision3DTest::GetHelpText() {
    return
        "[←↑↓→] 移動  [Tab] モード切替  [R] 読み直し\n"
        "[P] プレイヤーの形 カプセル/球\n"
        "カプセル×OBB(ピンクの箱)は未対応ですり抜ける\n"
        "茶=押せる箱 緑=トリガー 水色=球(Static)\n"
        "レイは進む向きへ10m（黄色の印=当たった場所）";
}

}
