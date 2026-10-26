#pragma once
#include "Structs.h"

class GameObject;

/// <summary>
/// GameObject に付ける「物理で動く」ための部品（重力・速度・質量・跳ね返り）。
/// 既定では回転しない（キャラクター向け）。SetUseRotation(true) にすると、ぶつかった所や AddImpulseAtPosition で回り、
/// 転がる・倒れる・積んだ箱が崩れる（球・OBB・カプセル・2D の円。AABB・2D の四角は形が回らないので回転しない）。
/// GameObject::AddRigidbody() で付ける。計算は GameObjectManager が固定間隔（既定 1/60 秒）でまとめて行う。
///
/// 使い方の例（Initialize の中で）:
///   AddCollider(Collider::MakeSphere(0.5f));
///   Rigidbody* body = AddRigidbody();
///   body->SetRestitution(0.8f);                // よく跳ねる
///   // Update の中で
///   if (jump && body->IsGrounded()) { body->AddImpulse({ 0.0f, 5.0f, 0.0f }); }
///   body->AddForce({ moveX * 20.0f, 0.0f, 0.0f });   // 押し続ける力（毎フレーム呼ぶ）
///
/// 位置は今まで通り transform.translate。コードで直接動かしてもよい（ワープ扱いになる）。
/// 当たり判定で押し戻されるには、コライダーが kDynamic（既定）であること。
/// </summary>
class Rigidbody {
public:
    // 速度（1秒あたりの移動量）。直接読み書きしてよい（例: 横の速度だけ決めたいとき velocity.x = 5.0f）
    Vector3 velocity = { 0.0f, 0.0f, 0.0f };

    // --- 力を加える ---
    // 押し続ける力（ニュートン相当）。1フレームに1回呼べば、そのフレームの間ずっと押した扱いになる
    void AddForce(const Vector3& force);
    // 瞬間的に速度を変える（ジャンプ・爆発・打撃）。速度の変化 = impulse / 質量
    void AddImpulse(const Vector3& impulse);

    // --- 設定 ---
    // 重さ（0 より大きい値。既定 1）。重い物ほど押されにくく、AddForce / AddImpulse で動きにくい
    void SetMass(float newMass);
    float GetMass() const { return mass; }
    // 重力を受けるか（既定 true）と、その倍率（既定 1。2 なら2倍の速さで落ちる）
    void SetUseGravity(bool use) { useGravity = use; }
    bool GetUseGravity() const { return useGravity; }
    void SetGravityScale(float scale) { gravityScale = scale; }
    float GetGravityScale() const { return gravityScale; }
    // 空気抵抗（0 で無し。大きいほど早く止まる。目安 0〜10）
    void SetLinearDamping(float damping) { linearDamping = damping; }
    float GetLinearDamping() const { return linearDamping; }
    // 跳ね返り（0 = 跳ねない、1 = 同じ速さで跳ね返る）。ぶつかった2つのうち大きい方が使われる
    void SetRestitution(float value) { restitution = value; }
    float GetRestitution() const { return restitution; }
    // 摩擦（0 = つるつる、1 = すぐ止まる）。ぶつかった2つの小さい方が使われる（Rigidbody が無い物は 0.4）。
    // キーで速度を直接決めるキャラクターは 0 にするとよい（壁に押し付けたとき、摩擦で落ちずに張り付くのを防ぐ）
    void SetFriction(float value) { friction = value; }
    float GetFriction() const { return friction; }
    // 乗り越えられる段差の高さ（キャラクター向け。既定 -1 = 制限なし）。
    // 0 以上にすると、段差の角や急な面に横からぶつかっても上へ押し出されなくなり（ジャンプせずに浮かない）、
    // 進んでいる向きにあるこの高さ以下の段差にだけ乗り上げる。0 なら段差には乗らない。
    // 制限なしのときは今まで通りの物理（球は角に当たると斜め上に押され、半径より低い段差は乗り越える）。
    // 球・カプセルのコライダー向け（箱のコライダーは、速く動くと低い段差に乗ってしまうことがある）
    void SetStepHeight(float height) { stepHeight = height; }
    float GetStepHeight() const { return stepHeight; }
    // 段差に乗ったとき、見た目をこの秒数かけてなめらかに持ち上げる（既定 0.1。0 なら一瞬で上がる）。
    // 当たり判定の位置（transform.translate）は一瞬で上がり、描画位置（GetRenderPosition）だけが遅れて追いつく
    void SetStepSmoothTime(float seconds) { stepSmoothTime = seconds; }
    float GetStepSmoothTime() const { return stepSmoothTime; }
    // キネマティック: 重力・力・ぶつかりで速度が変わらない（velocity どおりに動くだけ）。
    // 動く床・エレベーターなど「コードで決めた通りに動き、乗った物を押す」物に使う。相手からは無限に重い物に見える
    void SetKinematic(bool kinematic) { isKinematic = kinematic; }
    bool IsKinematic() const { return isKinematic; }
    // 軸ごとに動かなくする（2D なら z、見下ろし型なら y を固定する等）
    void SetFreezePosition(bool x, bool y, bool z);
    // 描画時に物理の位置の間をなめらかにつなぐか（既定 true。144Hz などでカクつかない）
    void SetInterpolate(bool enable) { interpolate = enable; }
    bool GetInterpolate() const { return interpolate; }

    // --- 回転（SetUseRotation(true) のときだけ） ---
    // 回転の速さ（ワールドの軸のまわりに 1 秒あたり何ラジアン。向きは右ねじ）。直接読み書きしてよい
    Vector3 angularVelocity = { 0.0f, 0.0f, 0.0f };
    // 物理で回るようにする（既定 false）。回る量は付いている最初の 3D コライダー（2D なら円）の形と質量から自動で決まる
    void SetUseRotation(bool use);
    bool GetUseRotation() const { return useRotation; }
    // ねじる力（毎フレーム呼ぶ）・瞬間的に回す
    void AddTorque(const Vector3& torque);
    void AddAngularImpulse(const Vector3& impulse);
    // ワールドの position を押す（ずれた所を押すほど回る。打撃・爆風など）。回転しない物は AddForce / AddImpulse と同じ
    void AddForceAtPosition(const Vector3& force, const Vector3& position);
    void AddImpulseAtPosition(const Vector3& impulse, const Vector3& position);
    // 回転の抵抗（0 で無し。転がり続ける球を止めるのにも使う。既定 0.05）
    void SetAngularDamping(float damping) { angularDamping = damping; }
    float GetAngularDamping() const { return angularDamping; }
    // ワールドの軸ごとに回らなくする（2D なら x と y を固定して z だけ回す。横倒しにしたくないなら x と z）
    void SetFreezeRotation(bool x, bool y, bool z);
    // 今の向き（transform.rotate と同じ向き）。transform.rotate を直接変えてもよい（ワープ扱い）
    Quaternion GetRotation() const { return orientation; }
    void SetRotation(const Quaternion& rotation);
    // ワールドの position の点が動いている速さ（回転の分を含む）
    Vector3 GetPointVelocity(const Vector3& position) const;

    // --- 状態 ---
    // 足元に何かがあるか（重力と反対向きの面に乗っている）。最後の物理計算の結果
    bool IsGrounded() const { return isGrounded; }
    // 乗っている面の向き（IsGrounded のときだけ意味がある）。坂道の判定などに使う
    const Vector3& GetGroundNormal() const { return groundNormal; }
    // 描画に使う位置（補間・段差のなめらかな持ち上げ込み）。カメラで追いかけるときはこちらを使うとカクつかない
    Vector3 GetRenderPosition() const;
    // 0 なら動かない（キネマティック）。押し戻しの分け方に使う
    float GetInverseMass() const { return isKinematic ? 0.0f : 1.0f / mass; }
    // 回る物か（SetUseRotation(true)・キネマティックでない・回れる形）
    bool IsRotating() const { return useRotation && !isKinematic && hasInertia; }
    // ワールドの回す力 v に対する「回りやすさ」を掛けた回転の速さの変化（慣性テンソルの逆行列 × v）。回らない物は 0
    Vector3 ApplyInverseInertia(const Vector3& v) const;

    GameObject* GetOwner() const { return owner; }

private:
    // 計算は GameObjectManager / CollisionManager が行う
    friend class GameObject;
    friend class GameObjectManager;
    friend class CollisionManager;

    GameObject* owner = nullptr;

    float mass = 1.0f;
    bool useGravity = true;
    float gravityScale = 1.0f;
    float linearDamping = 0.0f;
    float restitution = 0.0f;
    float friction = 0.4f;
    float stepHeight = -1.0f;
    float stepSmoothTime = 0.1f;
    bool isKinematic = false;
    bool freezeX = false;
    bool freezeY = false;
    bool freezeZ = false;
    bool interpolate = true;

    // AddForce / AddImpulse でたまった「速度の変化」（次の物理ステップで足す）
    Vector3 pendingVelocityChange = { 0.0f, 0.0f, 0.0f };

    bool isGrounded = false;
    // 1つ前の物理ステップで接地していたか（地面への吸い付けに使う）
    bool wasGrounded = false;
    Vector3 groundNormal = { 0.0f, 1.0f, 0.0f };

    // 補間用: 最後の物理ステップの前の位置 / 物理が最後に決めた位置 / 2つの間のどこを描くか（0〜1）
    Vector3 previousPosition = { 0.0f, 0.0f, 0.0f };
    Vector3 physicsPosition = { 0.0f, 0.0f, 0.0f };
    float renderAlpha = 1.0f;
    // まだ1回も物理計算していない（位置の記録が無い）
    bool hasPhysicsPosition = false;
    // 段差に乗ったときの見た目のずれ（描画位置に足す。GameObjectManager が毎フレーム 0 に近づける）
    Vector3 stepSmoothOffset = { 0.0f, 0.0f, 0.0f };

    // 回転
    // 持ち主のコライダーの形と質量から、ローカルの軸ごとの回りにくさの逆数を決める（GameObjectManager が物理の前に呼ぶ）
    void UpdateInertia();
    // orientation を transform.rotate に書き込む
    void WriteRotation();
    bool useRotation = false;
    float angularDamping = 0.05f;
    bool freezeRotationX = false;
    bool freezeRotationY = false;
    bool freezeRotationZ = false;
    // ローカルの X / Y / Z 軸のまわりの「回りにくさ」の逆数（0 ならその軸では回らない）と、回れる形か
    Vector3 localInverseInertia = { 0.0f, 0.0f, 0.0f };
    bool hasInertia = false;
    Quaternion orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // 補間用: 最後の物理ステップの前の向き
    Quaternion previousOrientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // 最後に transform.rotate に書いた値（違っていたらコードで向きを変えた = ワープ）
    Vector3 writtenRotation = { 0.0f, 0.0f, 0.0f };
    bool hasOrientation = false;
    Vector3 pendingAngularVelocityChange = { 0.0f, 0.0f, 0.0f };
};
