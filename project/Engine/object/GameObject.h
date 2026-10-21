#pragma once
#include "Collider.h"
#include "Rigidbody.h"
#include "Structs.h"
#include <memory>
#include <string>
#include <vector>

class GameObjectManager;
class LevelSpawner;
struct LevelObjectData;

/// <summary>
/// ゲームに登場する「物」（プレイヤー・敵・弾・アイテムなど）の基底クラス。
/// 自分で new せず、シーンが持つ GameObjectManager の Create<T>() で作ること。
/// 作った物はマネージャーが毎フレーム Update / Draw を呼び、Destroy() されたらフレームの最後に片付ける。
///
/// 使い方の例:
///   class Enemy : public GameObject {
///   public:
///       explicit Enemy(float speed) : speed(speed) {}
///       void Initialize() override { SetTag("Enemy"); }
///       void Update() override {
///           transform.translate.x -= speed * Time::GetDeltaTime();
///           if (transform.translate.x < 0.0f) { Destroy(); } // 画面外に出たら消える
///       }
///       void Draw() override { sprite->Draw(); }
///   private:
///       float speed = 0.0f;
///   };
///
///   // シーン側（メンバーに GameObjectManager objects; を持つ）
///   Enemy* enemy = objects.Create<Enemy>(100.0f); // コンストラクタの引数をそのまま渡せる
///   enemy->transform.translate = { 1280.0f, 360.0f, 0.0f };
///   objects.Update(); // シーンの Update で
///   objects.Draw();   // シーンの Draw で
///   objects.Clear();  // シーンの Finalize で
/// </summary>
class GameObject {
public:
    virtual ~GameObject() = default;

    // --- 派生クラスで必要なものだけ override する ---
    // Create<T>() の中で1回だけ呼ばれる（コンストラクタと違い、GetManager() が使える）
    virtual void Initialize() {}
    // 毎フレーム呼ばれる（非アクティブ・Destroy済みのときは呼ばれない）
    virtual void Update() {}
    // 毎フレーム呼ばれる（非アクティブ・Destroy済みのときは呼ばれない）
    virtual void Draw() {}
    // マネージャーから取り除かれる直前に1回だけ呼ばれる（後片付け用）
    virtual void Finalize() {}
    // レベルデータ（LevelSpawner）から作られたとき、Initialize の後に1回だけ呼ばれる。
    // 呼ばれた時点で transform・名前・コライダーは配置どおりに設定済み。独自の項目は data.raw から読む
    // （例: hp = int(data.raw["hp"].AsNumber());）。使うときは LevelData.h を include する
    virtual void OnLevelLoad(const LevelObjectData& /*data*/) {}

    // --- 名前とタグ ---
    // 名前: 1つの物を探すとき用（例: "Player"）。タグ: 種類で探すとき用（例: "Enemy"）
    void SetName(const std::string& newName);
    const std::string& GetName() const { return name; }
    void SetTag(const std::string& newTag);
    const std::string& GetTag() const { return tag; }
    bool HasTag(const std::string& checkTag) const;

    // --- 有効/無効と削除 ---
    // false にすると Update / Draw が呼ばれなくなる（消えはしない。true に戻せば再開する）
    void SetActive(bool active);
    bool IsActive() const { return isActive; }
    // フレームの最後にまとめて消える（Update中に自分や他の物に呼んでも安全）
    void Destroy();
    bool IsDestroyed() const { return isDestroyed; }

    // 自分を管理しているマネージャー（Update中に弾を撃つ・仲間を探すときに使う）
    GameObjectManager* GetManager() const { return manager; }
    // レベルデータ（LevelSpawner）から作られた物なら true
    bool IsFromLevel() const { return spawnedBy != nullptr; }

    // --- 当たり判定 ---
    // コライダーを付ける（例: AddCollider(Collider::MakeCircle(16.0f))）。付けたコライダーのポインタを返す。
    // 1つの物に複数付けてもよい。コライダーは持ち主と一緒に消える
    Collider* AddCollider(std::unique_ptr<Collider> collider);
    const std::vector<std::unique_ptr<Collider>>& GetColliders() const { return colliders; }
    // index 番目のコライダー（無ければ nullptr）。1つしか付けていないなら GetCollider() でよい
    Collider* GetCollider(size_t index = 0) const;

    // --- 物理 ---
    // 物理（重力・速度・跳ね返り）で動くようにする。2回呼んでも1つだけ（同じものを返す）
    Rigidbody* AddRigidbody();
    // 付いていなければ nullptr
    Rigidbody* GetRigidbody() const { return rigidbody.get(); }

    // ぶつかったときに呼ばれる（必要なものだけ override する）。
    // Enter: ぶつかり始めたフレーム / Stay: ぶつかっている間毎フレーム / Exit: 離れたフレーム
    // 中で Destroy() や Create() を呼んでも安全（実際の削除・追加はフレームの最後）
    virtual void OnCollisionEnter(const CollisionEvent& /*event*/) {}
    virtual void OnCollisionStay(const CollisionEvent& /*event*/) {}
    virtual void OnCollisionExit(const CollisionEvent& /*event*/) {}

    // 位置・回転・大きさ。初心者でも触りやすいよう public メンバーにしている
    // 2Dでは translate.x / translate.y を画面上の位置（ゲーム座標）として使えばよい
    Transform transform{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

private:
    // manager / isDestroyed などはマネージャーだけが直接書き換える
    friend class GameObjectManager;
    // spawnedBy は LevelSpawner だけが設定する
    friend class LevelSpawner;

    GameObjectManager* manager = nullptr;
    // 自分を作った LevelSpawner（読み直しで「そのスポナーが作った物」だけを消すための印。指す先には触らない）
    const LevelSpawner* spawnedBy = nullptr;
    std::string name;
    std::string tag;
    bool isActive = true;
    bool isDestroyed = false;
    // Finalize() を呼び終えたか（Finalize の中で別の物が Destroy されても、その物の Finalize を飛ばさないため）
    bool isFinalized = false;
    // 付いているコライダー（CollisionManager が毎フレームここから集めるので、登録・解除の手間はない）
    std::vector<std::unique_ptr<Collider>> colliders;
    // 物理（AddRigidbody されるまで nullptr）
    std::unique_ptr<Rigidbody> rigidbody;
};
