#include "GameObjectManager.h"
#include "Calculation.h"
#include "DebugDraw.h"
#include "GameTime.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <unordered_set>

namespace {
    // 1フレームに計算する物理の最大回数（これ以上たまった時間は捨てる）
    constexpr int kMaxPhysicsStepsPerFrame = 5;
    // 当たり判定の表示（[F7]）: 速度の矢印の長さ（速さ 1 あたり）と、非アクティブの物の線の濃さ
    constexpr float kVelocityArrowScale = 0.2f;
    constexpr float kInactiveColliderAlpha = 0.3f;

    bool IsSamePosition(const Vector3& a, const Vector3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
}

GameObjectManager::~GameObjectManager() {
    Clear();
}

void GameObjectManager::Update() {
    // 1. 全員の Update（この間に Create された物は pendingObjects に入るので、ループは壊れない）
    isUpdating = true;
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (object->IsActive() && !object->IsDestroyed()) {
            object->Update();
        }
    }
    isUpdating = false;

    // 2. 物理と当たり判定（重力・速度で動かす → 押し戻し → OnCollisionEnter / Stay / Exit）
    //    通知の中で Create / Destroy されてもよいよう、ループ中扱いにしておく
    isUpdating = true;
    UpdatePhysics(CollectAliveObjects());
    isUpdating = false;

    // 3. Update 中に作られた物を本リストへ（次のフレームから Update / Draw される）
    MovePendingObjects();

    // 4. Destroy() された物をまとめて片付ける
    RemoveDestroyed();
}

void GameObjectManager::Draw() {
    // Rigidbody の物は、物理の位置の間を補間した位置で描く（描き終えたら元に戻す）
    BeginRenderInterpolation();
    // Draw 中に Create されても objects が増えないよう、Update と同じく一時置き場を使わせる
    isUpdating = true;
    // ModelObject は描かずにまとめ役へ預け、最後に同じモデルごとまとめて描く
    if (isAutoBatching) {
        modelBatcher.Begin(false);
    }
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (object->IsActive() && !object->IsDestroyed()) {
            object->Draw();
        }
    }
    modelBatcher.Flush();
    isUpdating = false;
    // 補間した描画の位置に合わせて、見た目のモデルと重なるように描く
    if (DebugDraw::IsColliderVisible()) {
        AddColliderDebugLines();
    }
    EndRenderInterpolation();
}

void GameObjectManager::AddColliderDebugLines() const {
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (object->IsDestroyed()) {
            continue;
        }
        const Rigidbody* body = object->GetRigidbody();
        // 種類で色を分ける。非アクティブの物は判定されないので暗くする
        for (const std::unique_ptr<Collider>& collider : object->GetColliders()) {
            if (!collider->IsEnabled()) {
                continue;
            }
            Vector4 color = DebugDraw::kGreen;
            if (collider->GetType() == ColliderType::kTrigger) {
                color = DebugDraw::kYellow;
            } else if (collider->GetType() == ColliderType::kStatic) {
                color = DebugDraw::kGray;
            } else if (body && body->IsKinematic()) {
                color = DebugDraw::kBlue;
            }
            if (!object->IsActive()) {
                color.w = kInactiveColliderAlpha;
            }
            switch (collider->GetShape()) {
            case ColliderShape::kSphere: DebugDraw::Sphere(collider->GetWorldSphere(), color); break;
            case ColliderShape::kAABB: DebugDraw::Box(collider->GetWorldAABB(), color); break;
            case ColliderShape::kOBB: DebugDraw::Box(collider->GetWorldOBB(), color); break;
            case ColliderShape::kCapsule: DebugDraw::Capsule(collider->GetWorldCapsule(), color); break;
            case ColliderShape::kCircle: DebugDraw::Circle2D(collider->GetWorldCircle(), color); break;
            case ColliderShape::kRect: DebugDraw::Rect2D(collider->GetWorldRect(), color); break;
            }
        }
        // 動いている Rigidbody の速度（隠れない矢印）。2D か 3D かは最初のコライダーで決める
        if (!body || !object->IsActive() || body->IsKinematic()) {
            continue;
        }
        const Vector3& position = object->transform.translate;
        const Vector3 arrow = body->velocity * kVelocityArrowScale;
        const Collider* first = object->GetCollider();
        if (first && first->Is2D()) {
            DebugDraw::Arrow2D({ position.x, position.y }, { position.x + arrow.x, position.y + arrow.y }, DebugDraw::kMagenta);
        } else {
            DebugDraw::Arrow(position, position + arrow, DebugDraw::kMagenta, 0.0f, false);
        }
    }
}

void GameObjectManager::DrawShadow(const Matrix4x4& lightViewProjection) {
    BeginRenderInterpolation();
    isUpdating = true;
    if (isAutoBatching) {
        modelBatcher.Begin(true);
    }
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (object->IsActive() && !object->IsDestroyed()) {
            object->DrawShadow(lightViewProjection);
        }
    }
    modelBatcher.Flush();
    isUpdating = false;
    EndRenderInterpolation();
}

void GameObjectManager::SetFixedTimeStep(float seconds) {
    assert(seconds > 0.0f && "GameObjectManager: 物理の間隔は 0 より大きくしてください");
    if (seconds > 0.0f) {
        fixedTimeStep = seconds;
    }
}

void GameObjectManager::UpdatePhysics(const std::vector<GameObject*>& alive) {
    std::vector<Rigidbody*> bodies;
    for (GameObject* object : alive) {
        if (object->IsActive() && object->rigidbody) {
            bodies.push_back(object->rigidbody.get());
        }
    }
    // 前のフレームの物理の後にコードで位置を変えられた物（ワープ・初めての物）は、補間せずその位置から始める
    for (Rigidbody* body : bodies) {
        const Vector3& position = body->owner->transform.translate;
        if (!body->hasPhysicsPosition || !IsSamePosition(position, body->physicsPosition)) {
            body->previousPosition = position;
            body->physicsPosition = position;
            body->hasPhysicsPosition = true;
            body->stepSmoothOffset = { 0.0f, 0.0f, 0.0f }; // ワープしたら段差の持ち上げ途中の見た目も捨てる
        }
        // 回る物: 形と質量から回りやすさを決め、コードで向きを変えられていたら（初めても）その向きから始める
        body->UpdateInertia();
        if (body->useRotation && (!body->hasOrientation || !IsSamePosition(body->owner->transform.rotate, body->writtenRotation))) {
            body->orientation = Calculation::MakeQuaternionFromEuler(body->owner->transform.rotate);
            body->previousOrientation = body->orientation;
            body->writtenRotation = body->owner->transform.rotate;
            body->hasOrientation = true;
        }
    }

    // たまった時間の分だけ固定間隔で計算する（重いフレームは複数回、速い PC では計算しないフレームもある）
    collisionManager.SetPhysicsParameters(gravity, fixedTimeStep);
    physicsAccumulator += Time::GetDeltaTime();
    int steps = 0;
    collisionManager.BeginFrame();
    while (physicsAccumulator >= fixedTimeStep && steps < kMaxPhysicsStepsPerFrame) {
        for (Rigidbody* body : bodies) {
            body->previousPosition = body->owner->transform.translate;
            body->previousOrientation = body->orientation;
            body->wasGrounded = body->isGrounded;
            body->isGrounded = false; // 当たり判定で地面に触れたら true になる
            IntegrateBody(*body, fixedTimeStep);
        }
        collisionManager.Step(alive);
        physicsAccumulator -= fixedTimeStep;
        ++steps;
    }
    if (steps == kMaxPhysicsStepsPerFrame) {
        // 処理が追いつかない（止まっていた・極端に重い）ときは、たまった分を捨てる（何十回も計算して余計に重くならないように）
        physicsAccumulator = (std::min)(physicsAccumulator, fixedTimeStep * 0.99f);
    }
    if (steps == 0) {
        // 物理を計算しないフレームでも、コードで動かした物の当たり判定と押し戻しは毎フレーム行う
        collisionManager.Step(alive);
    }
    lastPhysicsStepCount = steps;

    // 次のステップまでの端数の割合で、描画時に前の位置と今の位置の間を補間する
    const float alpha = std::clamp(physicsAccumulator / fixedTimeStep, 0.0f, 1.0f);
    for (Rigidbody* body : bodies) {
        body->physicsPosition = body->owner->transform.translate;
        body->renderAlpha = alpha;
        // 段差に乗ったときの見た目のずれを 0 に近づける（stepSmoothTime 秒でほぼ 0 になる減り方）
        if (body->stepSmoothTime > 0.0f) {
            body->stepSmoothOffset = body->stepSmoothOffset * std::exp(-3.0f * Time::GetDeltaTime() / body->stepSmoothTime);
        } else {
            body->stepSmoothOffset = { 0.0f, 0.0f, 0.0f };
        }
    }

    collisionManager.EndFrame(alive);
}

void GameObjectManager::IntegrateBody(Rigidbody& body, float step) const {
    if (!body.isKinematic) {
        body.velocity += body.pendingVelocityChange;
        body.pendingVelocityChange = { 0.0f, 0.0f, 0.0f };
        if (body.useGravity) {
            body.velocity += gravity * (body.gravityScale * step);
        }
        if (body.linearDamping > 0.0f) {
            // 1秒あたり linearDamping の割合で減速（間隔が変わっても同じ減り方になる形）
            body.velocity = body.velocity * (1.0f / (1.0f + body.linearDamping * step));
        }
    }
    if (body.freezeX) { body.velocity.x = 0.0f; }
    if (body.freezeY) { body.velocity.y = 0.0f; }
    if (body.freezeZ) { body.velocity.z = 0.0f; }
    body.owner->transform.translate += body.velocity * step;
    IntegrateRotation(body, step);
}

void GameObjectManager::IntegrateRotation(Rigidbody& body, float step) const {
    if (!body.useRotation) {
        return;
    }
    if (!body.isKinematic) {
        // たまった回す力を、回りやすさに合わせて回転の速さに変える（回れない形なら回らない）
        body.angularVelocity += body.ApplyInverseInertia(body.pendingAngularVelocityChange);
        body.pendingAngularVelocityChange = { 0.0f, 0.0f, 0.0f };
        if (!body.hasInertia) {
            body.angularVelocity = { 0.0f, 0.0f, 0.0f };
        }
        if (body.angularDamping > 0.0f) {
            body.angularVelocity = body.angularVelocity * (1.0f / (1.0f + body.angularDamping * step));
        }
    }
    // キネマティックも angularVelocity どおりに回る（回る床など）
    if (body.freezeRotationX) { body.angularVelocity.x = 0.0f; }
    if (body.freezeRotationY) { body.angularVelocity.y = 0.0f; }
    if (body.freezeRotationZ) { body.angularVelocity.z = 0.0f; }
    const Vector3& w = body.angularVelocity;
    if (w.x == 0.0f && w.y == 0.0f && w.z == 0.0f) {
        return;
    }
    // 向きの変化 = 0.5 × (回転の速さ, 0) × 向き（ワールドの軸まわりの回転）
    Quaternion& q = body.orientation;
    const float half = 0.5f * step;
    const Quaternion change = {
        (w.x * q.w + w.y * q.z - w.z * q.y) * half,
        (w.y * q.w + w.z * q.x - w.x * q.z) * half,
        (w.z * q.w + w.x * q.y - w.y * q.x) * half,
        (-w.x * q.x - w.y * q.y - w.z * q.z) * half,
    };
    q = Calculation::Normalize({ q.x + change.x, q.y + change.y, q.z + change.z, q.w + change.w });
    body.WriteRotation();
}

void GameObjectManager::BeginRenderInterpolation() {
    if (renderInterpolationDepth++ > 0) {
        return; // すでに入れ替え中
    }
    savedPhysicsPositions.clear();
    for (const std::unique_ptr<GameObject>& object : objects) {
        const Rigidbody* body = object->rigidbody.get();
        // 補間 OFF でも段差の持ち上げ途中ならずれを足して描く（GetRenderPosition が両方を扱う）
        if (!body || object->IsDestroyed()) {
            continue;
        }
        savedPhysicsPositions.push_back({ object.get(), object->transform.translate, object->transform.rotate });
        object->transform.translate = body->GetRenderPosition();
        // 回る物は向きも前のステップとの間をつなぐ
        if (body->useRotation && body->interpolate && body->hasOrientation) {
            object->transform.rotate =
                Calculation::MakeEulerFromQuaternion(Calculation::Slerp(body->previousOrientation, body->orientation, body->renderAlpha));
        }
    }
}

void GameObjectManager::EndRenderInterpolation() {
    assert(renderInterpolationDepth > 0 && "GameObjectManager: EndRenderInterpolation が Begin より多く呼ばれました");
    if (renderInterpolationDepth <= 0 || --renderInterpolationDepth > 0) {
        return;
    }
    for (const SavedTransform& saved : savedPhysicsPositions) {
        saved.object->transform.translate = saved.translate;
        saved.object->transform.rotate = saved.rotate;
    }
    savedPhysicsPositions.clear();
}

GameObject* GameObjectManager::Find(const std::string& name) const {
    GameObject* found = nullptr;
    ForEachAlive([&](GameObject* object) {
        if (object->GetName() == name) {
            found = object;
        }
        return found != nullptr;
    });
    return found;
}

GameObject* GameObjectManager::FindByTag(const std::string& tag) const {
    GameObject* found = nullptr;
    ForEachAlive([&](GameObject* object) {
        if (object->HasTag(tag)) {
            found = object;
        }
        return found != nullptr;
    });
    return found;
}

std::vector<GameObject*> GameObjectManager::FindAllByTag(const std::string& tag) const {
    std::vector<GameObject*> result;
    ForEachAlive([&](GameObject* object) {
        if (object->HasTag(tag)) {
            result.push_back(object);
        }
        return false; // 最後まで探す
    });
    return result;
}

void GameObjectManager::Clear() {
    if (isUpdating) {
        // Update / Draw のループ中に消すとループが壊れるので、全員に Destroy の印だけ付けて
        // フレームの最後（RemoveDestroyed）で片付けてもらう
        for (const std::unique_ptr<GameObject>& object : objects) {
            object->Destroy();
        }
        for (const std::unique_ptr<GameObject>& object : pendingObjects) {
            object->Destroy();
        }
        return;
    }

    // 当たり判定が覚えている「当たっている組」も全部忘れる（全員消えるので Exit は送らない）
    collisionManager.Reset();

    // 先に手元へ移してから Finalize を呼ぶ（Finalize の中で Find などが呼ばれても空のリストを見るだけで安全）
    std::vector<std::unique_ptr<GameObject>> removing = std::move(objects);
    objects.clear();
    for (std::unique_ptr<GameObject>& object : pendingObjects) {
        removing.push_back(std::move(object));
    }
    pendingObjects.clear();

    for (const std::unique_ptr<GameObject>& object : removing) {
        object->Finalize();
    }
    // removing がここで破棄され、unique_ptr が中身を delete する
}

size_t GameObjectManager::GetCount() const {
    size_t count = 0;
    ForEachAlive([&count](GameObject*) {
        ++count;
        return false;
    });
    return count;
}

std::vector<GameObject*> GameObjectManager::CollectAliveObjects() const {
    // pendingObjects（このフレームに作られた物）は次のフレームから判定する
    std::vector<GameObject*> result;
    result.reserve(objects.size());
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (!object->IsDestroyed()) {
            result.push_back(object.get());
        }
    }
    return result;
}

void GameObjectManager::MovePendingObjects() {
    for (std::unique_ptr<GameObject>& object : pendingObjects) {
        objects.push_back(std::move(object));
    }
    pendingObjects.clear();
}

void GameObjectManager::RemoveDestroyed() {
    // 消える物の後片付けを先に済ませる
    // （Finalize の中で Create されても objects が増えないよう、ループ中扱いにしておく）
    isUpdating = true;
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (object->IsDestroyed() && !object->isFinalized) {
            object->Finalize();
            object->isFinalized = true;
        }
    }
    isUpdating = false;

    // delete する前に、当たり判定が覚えている消える物の情報を片付けてもらう
    // （相手が消えた組の Exit は、次のフレームの当たり判定で残った側に届く）
    std::unordered_set<const GameObject*> removing;
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (object->isFinalized) {
            removing.insert(object.get());
        }
    }
    if (!removing.empty()) {
        collisionManager.RemoveObjects(removing);
    }

    // erase-remove: Finalize 済みの物を後ろへ寄せてまとめて消す（unique_ptr が delete してくれる）。
    // Finalize の中で新たに Destroy された物はまだ Finalize していないので、次のフレームで片付ける
    objects.erase(
        std::remove_if(objects.begin(), objects.end(),
            [](const std::unique_ptr<GameObject>& object) { return object->isFinalized; }),
        objects.end());
}
