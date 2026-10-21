#include "GameObjectManager.h"
#include "GameTime.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <unordered_set>

namespace {
    // 1フレームに計算する物理の最大回数（これ以上たまった時間は捨てる）
    constexpr int kMaxPhysicsStepsPerFrame = 5;

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
    for (const std::unique_ptr<GameObject>& object : objects) {
        if (object->IsActive() && !object->IsDestroyed()) {
            object->Draw();
        }
    }
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
    }

    // たまった時間の分だけ固定間隔で計算する（重いフレームは複数回、速い PC では計算しないフレームもある）
    collisionManager.SetPhysicsParameters(gravity, fixedTimeStep);
    physicsAccumulator += Time::GetDeltaTime();
    int steps = 0;
    collisionManager.BeginFrame();
    while (physicsAccumulator >= fixedTimeStep && steps < kMaxPhysicsStepsPerFrame) {
        for (Rigidbody* body : bodies) {
            body->previousPosition = body->owner->transform.translate;
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
        savedPhysicsPositions.emplace_back(object.get(), object->transform.translate);
        object->transform.translate = body->GetRenderPosition();
    }
}

void GameObjectManager::EndRenderInterpolation() {
    assert(renderInterpolationDepth > 0 && "GameObjectManager: EndRenderInterpolation が Begin より多く呼ばれました");
    if (renderInterpolationDepth <= 0 || --renderInterpolationDepth > 0) {
        return;
    }
    for (const auto& [object, position] : savedPhysicsPositions) {
        object->transform.translate = position;
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
