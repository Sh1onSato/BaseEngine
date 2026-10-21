#include "Rigidbody.h"
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
