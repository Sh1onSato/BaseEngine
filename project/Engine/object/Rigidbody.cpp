#include "Rigidbody.h"
#include "Calculation.h"
#include "Collider.h"
#include "GameObject.h"
#include "GameTime.h"
#include <cassert>

void Rigidbody::AddForce(const Vector3& force) {
    if (isKinematic) {
        return;
    }
    // 物理の計算回数はフレームによって 0〜数回と変わるので、「このフレームの時間ぶん押した結果」の速度の変化として貯めておく
    pendingVelocityChange += force * (Time::GetDeltaTime() / mass);
}

void Rigidbody::AddImpulse(const Vector3& impulse) {
    if (isKinematic) {
        return;
    }
    velocity += impulse * (1.0f / mass);
}

void Rigidbody::SetMass(float newMass) {
    assert(newMass > 0.0f && "Rigidbody: 質量は 0 より大きくしてください");
    if (newMass > 0.0f) {
        mass = newMass;
    }
}

void Rigidbody::SetFreezePosition(bool x, bool y, bool z) {
    freezeX = x;
    freezeY = y;
    freezeZ = z;
}

Vector3 Rigidbody::GetRenderPosition() const {
    const Vector3& current = owner->transform.translate;
    if (!interpolate || !hasPhysicsPosition) {
        return current + stepSmoothOffset;
    }
    return previousPosition + (current - previousPosition) * renderAlpha + stepSmoothOffset;
}

// ===== 回転 =====

void Rigidbody::SetUseRotation(bool use) {
    useRotation = use;
    // 次の物理計算で、今の transform.rotate から向きを取り直す
    hasOrientation = false;
}

void Rigidbody::AddTorque(const Vector3& torque) {
    if (isKinematic) {
        return;
    }
    // 回りやすさ（慣性）は物理の前に決まるので、ここでは「回す力 × 時間」として貯めておく
    pendingAngularVelocityChange += torque * Time::GetDeltaTime();
}

void Rigidbody::AddAngularImpulse(const Vector3& impulse) {
    if (isKinematic) {
        return;
    }
    pendingAngularVelocityChange += impulse;
}

void Rigidbody::AddForceAtPosition(const Vector3& force, const Vector3& position) {
    AddForce(force);
    AddTorque(Calculation::Cross(position - owner->transform.translate, force));
}

void Rigidbody::AddImpulseAtPosition(const Vector3& impulse, const Vector3& position) {
    AddImpulse(impulse);
    AddAngularImpulse(Calculation::Cross(position - owner->transform.translate, impulse));
}

void Rigidbody::SetFreezeRotation(bool x, bool y, bool z) {
    freezeRotationX = x;
    freezeRotationY = y;
    freezeRotationZ = z;
}

void Rigidbody::SetRotation(const Quaternion& rotation) {
    orientation = Calculation::Normalize(rotation);
    previousOrientation = orientation;
    hasOrientation = true;
    WriteRotation();
}

Vector3 Rigidbody::GetPointVelocity(const Vector3& position) const {
    if (!useRotation) {
        return velocity;
    }
    return velocity + Calculation::Cross(angularVelocity, position - owner->transform.translate);
}

Vector3 Rigidbody::ApplyInverseInertia(const Vector3& v) const {
    if (!IsRotating()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    // ローカルの軸ごとに「その軸まわりの回りやすさ」を掛けて足す（= 回転 × 対角の逆慣性 × 回転の逆 × v）
    const Vector3 axes[3] = {
        Calculation::RotateVector({ 1.0f, 0.0f, 0.0f }, orientation),
        Calculation::RotateVector({ 0.0f, 1.0f, 0.0f }, orientation),
        Calculation::RotateVector({ 0.0f, 0.0f, 1.0f }, orientation),
    };
    const float inverse[3] = { localInverseInertia.x, localInverseInertia.y, localInverseInertia.z };
    Vector3 result = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 3; ++i) {
        result += axes[i] * (inverse[i] * Calculation::Dot(axes[i], v));
    }
    // 固定したワールドの軸のまわりには回さない
    if (freezeRotationX) { result.x = 0.0f; }
    if (freezeRotationY) { result.y = 0.0f; }
    if (freezeRotationZ) { result.z = 0.0f; }
    return result;
}

void Rigidbody::UpdateInertia() {
    hasInertia = false;
    localInverseInertia = { 0.0f, 0.0f, 0.0f };
    if (!useRotation || !owner) {
        return;
    }
    // 最初の回れる形のコライダーで決める（中心は持ち主の位置とみなす。offset は考えない）
    for (const std::unique_ptr<Collider>& collider : owner->GetColliders()) {
        Vector3 inertia = { 0.0f, 0.0f, 0.0f };
        switch (collider->GetShape()) {
        case ColliderShape::kSphere: {
            const float r = collider->GetWorldSphere().radius;
            const float value = 0.4f * mass * r * r; // 中の詰まった球
            inertia = { value, value, value };
            break;
        }
        case ColliderShape::kOBB: {
            const Vector3 h = collider->GetWorldOBB().halfSize;
            // 中の詰まった箱（辺の長さ 2h）
            inertia = { mass / 3.0f * (h.y * h.y + h.z * h.z), mass / 3.0f * (h.x * h.x + h.z * h.z), mass / 3.0f * (h.x * h.x + h.y * h.y) };
            break;
        }
        case ColliderShape::kCapsule: {
            // ローカル Y に伸びた円柱（長さは両端の丸い所を含めた全体）とみなす
            const Capsule capsule = collider->GetWorldCapsule();
            const float r = capsule.radius;
            const float length = Calculation::Length(capsule.segment.end - capsule.segment.start) + r * 2.0f;
            const float side = mass * (3.0f * r * r + length * length) / 12.0f;
            inertia = { side, 0.5f * mass * r * r, side };
            break;
        }
        case ColliderShape::kCircle: {
            const float r = collider->GetWorldCircle().radius;
            const float value = 0.5f * mass * r * r; // 円板
            inertia = { value, value, value };
            break;
        }
        default:
            continue; // AABB・2D の四角は形が回らないので使わない
        }
        if (inertia.x > 0.0f && inertia.y > 0.0f && inertia.z > 0.0f) {
            localInverseInertia = { 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z };
            hasInertia = true;
        }
        return;
    }
}

void Rigidbody::WriteRotation() {
    if (!owner) {
        return;
    }
    owner->transform.rotate = Calculation::MakeEulerFromQuaternion(orientation);
    writtenRotation = owner->transform.rotate;
}
