#include "ObjectTest3DObjects.h"
#include "Calculation.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "Model.h"
#include "Object3d.h"
#include "PrimitiveMesh.h"
#include <algorithm>

namespace {
    const char* kPlayerTag = "Player";
    constexpr float kPlayerSpeed = 5.0f;           // 1秒あたりの移動量
    constexpr float kPlayerCapsuleRadius = 0.5f;
    constexpr float kPlayerCapsuleHeight = 1.0f;   // 両端の球の中心間の距離（全体の高さは 2.0）
    constexpr float kPlayerSphereRadius = 0.75f;
    constexpr float kMoveLimit = 11.5f;            // 床（半辺12）から落ちないよう動ける範囲
    // 床の上面（y = 0）とちょうど接すると「当たった・離れた」がちらつくことがあるので、少しだけ浮かせる
    constexpr float kFloatEpsilon = 0.01f;
    constexpr float kCrateHalfSize = 0.6f;
    const Vector3 kTriggerHalfSize = { 1.5f, 1.0f, 1.5f };
    constexpr float kTriggerPadThickness = 0.05f;

    const Vector4 kPlayerColor = { 0.2f, 0.4f, 1.0f, 1.0f };
    const Vector4 kPlayerTouchColor = { 0.6f, 0.9f, 1.0f, 1.0f };
    const Vector4 kCrateColor = { 0.6f, 0.4f, 0.2f, 1.0f };
    const Vector4 kTriggerColor = { 0.2f, 0.6f, 0.2f, 1.0f };
    const Vector4 kTriggerActiveColor = { 0.4f, 1.0f, 0.4f, 1.0f };
}

namespace ObjectTest {

// ===== メッシュ =====

Arena3DMeshes::Arena3DMeshes() = default;
// Model の中身はヘッダーで見えないので、デストラクタは .cpp に置く（unique_ptr<Model> の破棄に必要）
Arena3DMeshes::~Arena3DMeshes() = default;

void Arena3DMeshes::Initialize() {
    DirectXCommon* dxCommon = Framework::GetInstance()->GetDxCommon();
    // "white" はエンジン内部で作られる1x1の白画像。色は Object3d::SetColor で付ける
    cube = std::make_unique<Model>();
    cube->InitializeFromVertices(dxCommon, PrimitiveMesh::BuildCube(), "white");
    sphere = std::make_unique<Model>();
    sphere->InitializeFromVertices(dxCommon, PrimitiveMesh::BuildSphere(16, 12), "white");
}

// ===== MeshObject =====

MeshObject::MeshObject(Model* model, const Vector3& position, const Vector3& size, const Vector4& color)
    : model(model), color(color) {
    transform.translate = position;
    transform.scale = size;
}

MeshObject::~MeshObject() = default;

void MeshObject::CreateMesh() {
    object3d = std::make_unique<Object3d>();
    object3d->Initialize(Framework::GetInstance()->GetObject3dCommon());
    object3d->SetModel(model);
    // カメラは SetCamera しない → CameraManager のアクティブカメラが使われる
    SyncMesh();
}

void MeshObject::SyncMesh() {
    if (!object3d) {
        return;
    }
    object3d->SetScale(transform.scale);
    object3d->SetRotate(transform.rotate);
    object3d->SetTranslate(transform.translate);
    object3d->SetColor(color);
    object3d->Update();
}

void MeshObject::Draw() {
    if (!object3d) {
        return;
    }
    SyncMesh();
    object3d->Draw();
}

void MeshObject::DrawShadow(const Matrix4x4& lightViewProjection) {
    if (!object3d) {
        return;
    }
    // シャドウパスは通常の Draw より先なので、ここで先に最新の位置を反映しておく
    SyncMesh();
    object3d->DrawShadow(lightViewProjection);
}

// ===== Obstacle =====

Obstacle::Obstacle(Model* model, const std::string& name, const Vector3& position, const Vector3& size,
                   float rotateY, const Vector4& color, std::unique_ptr<Collider> collider)
    : MeshObject(model, position, size, color), obstacleName(name), pendingCollider(std::move(collider)) {
    transform.rotate.y = rotateY; // OBB は持ち主の回転に追従する（AABB・球は回転を無視する）
}

void Obstacle::Initialize() {
    SetName(obstacleName);
    SetTag("Obstacle");
    CreateMesh();
    if (pendingCollider) {
        Collider* collider = AddCollider(std::move(pendingCollider));
        collider->SetType(ColliderType::kStatic);
        collider->SetCollisionLayer(kWorld3DLayer);
    }
}

// ===== Player3D =====

Player3D::Player3D(const Arena3DMeshes& meshes, const Vector2& positionXZ, bool useSphere)
    : MeshObject(useSphere ? meshes.GetSphere() : meshes.GetCube(),
                 { positionXZ.x, GetCenterHeight(useSphere), positionXZ.y },
                 useSphere ? Vector3{ kPlayerSphereRadius * 2.0f, kPlayerSphereRadius * 2.0f, kPlayerSphereRadius * 2.0f }
                           : Vector3{ kPlayerCapsuleRadius * 2.0f, kPlayerCapsuleHeight + kPlayerCapsuleRadius * 2.0f, kPlayerCapsuleRadius * 2.0f },
                 kPlayerColor),
      useSphere(useSphere) {}

float Player3D::GetCenterHeight(bool useSphere) {
    // 底（中心から一番下までの距離）がちょうど床の上面 y = 0 の少し上に来る高さ
    float bottomDistance = useSphere ? kPlayerSphereRadius : kPlayerCapsuleHeight * 0.5f + kPlayerCapsuleRadius;
    return bottomDistance + kFloatEpsilon;
}

void Player3D::Initialize() {
    SetName(kPlayerTag);
    SetTag(kPlayerTag);
    CreateMesh();
    std::unique_ptr<Collider> shape = useSphere
        ? Collider::MakeSphere(kPlayerSphereRadius)
        : Collider::MakeCapsule(kPlayerCapsuleRadius, kPlayerCapsuleHeight);
    Collider* collider = AddCollider(std::move(shape));
    collider->SetType(ColliderType::kDynamic);
    collider->SetCollisionLayer(kPlayer3DLayer);
}

void Player3D::Update() {
    Move();
    // 重力は無いので、高さは毎フレーム固定する（押し戻しで上下にずれても戻す。床とは少し離してあるので当たらない）
    transform.translate.y = GetCenterHeight(useSphere);
    SetColor(contactCount > 0 ? kPlayerTouchColor : kPlayerColor);
}

void Player3D::Move() {
    Input* input = Framework::GetInstance()->GetInput();
    Vector3 direction = { 0.0f, 0.0f, 0.0f };
    if (input->PushKey(DIK_LEFT)) { direction.x -= 1.0f; }
    if (input->PushKey(DIK_RIGHT)) { direction.x += 1.0f; }
    if (input->PushKey(DIK_UP)) { direction.z += 1.0f; }   // 奥へ
    if (input->PushKey(DIK_DOWN)) { direction.z -= 1.0f; } // 手前へ

    if (Calculation::Length(direction) <= 0.0f) {
        return;
    }
    // 斜めでも速さが変わらないよう長さを1にそろえる
    facing = Calculation::Normalize(direction);
    transform.translate += facing * (kPlayerSpeed * Time::GetDeltaTime());
    transform.translate.x = std::clamp(transform.translate.x, -kMoveLimit, kMoveLimit);
    transform.translate.z = std::clamp(transform.translate.z, -kMoveLimit, kMoveLimit);
}

void Player3D::OnCollisionEnter(const CollisionEvent& /*event*/) {
    ++contactCount;
}

void Player3D::OnCollisionExit(const CollisionEvent& /*event*/) {
    // 相手が消えていても（event.other == nullptr）数は減らす
    --contactCount;
}

// ===== Crate3D =====

Crate3D::Crate3D(Model* model, const Vector2& positionXZ)
    : MeshObject(model, { positionXZ.x, kCrateHalfSize + kFloatEpsilon, positionXZ.y },
                 { kCrateHalfSize * 2.0f, kCrateHalfSize * 2.0f, kCrateHalfSize * 2.0f }, kCrateColor) {}

void Crate3D::Initialize() {
    SetName("Crate");
    SetTag("Crate");
    CreateMesh();
    Collider* collider = AddCollider(Collider::MakeBox({ kCrateHalfSize, kCrateHalfSize, kCrateHalfSize }));
    collider->SetType(ColliderType::kDynamic);
    collider->SetCollisionLayer(kWorld3DLayer);
}

void Crate3D::Update() {
    // プレイヤーと同じく高さは固定（斜めに押されて浮いたり沈んだりしないように）
    transform.translate.y = kCrateHalfSize + kFloatEpsilon;
}

// ===== TriggerZone =====

TriggerZone::TriggerZone(Model* model, const Vector2& positionXZ, int* entryCount)
    : MeshObject(model, { positionXZ.x, kTriggerPadThickness * 0.5f + kFloatEpsilon, positionXZ.y },
                 { kTriggerHalfSize.x * 2.0f, kTriggerPadThickness, kTriggerHalfSize.z * 2.0f }, kTriggerColor),
      entryCount(entryCount) {}

void TriggerZone::Initialize() {
    SetName("TriggerZone");
    SetTag("Trigger");
    CreateMesh();
    Collider* collider = AddCollider(Collider::MakeBox(kTriggerHalfSize));
    collider->SetType(ColliderType::kTrigger);
    collider->SetCollisionLayer(kTrigger3DLayer);
    // 見た目は床に敷いた薄い板だが、判定は板の上に立つ箱（底が床の上面）にする
    collider->SetOffset({ 0.0f, kTriggerHalfSize.y - transform.translate.y, 0.0f });
}

void TriggerZone::Update() {
    SetColor(insideCount > 0 ? kTriggerActiveColor : kTriggerColor);
}

void TriggerZone::OnCollisionEnter(const CollisionEvent& event) {
    ++insideCount;
    if (entryCount && event.other && event.other->HasTag(kPlayerTag)) {
        ++(*entryCount);
    }
}

void TriggerZone::OnCollisionExit(const CollisionEvent& /*event*/) {
    --insideCount;
}

}
