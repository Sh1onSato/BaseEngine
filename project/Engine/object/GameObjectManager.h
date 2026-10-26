#pragma once
#include "CollisionManager.h"
#include "GameObject.h"
#include "ModelBatcher.h"
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/// <summary>
/// GameObject をまとめて持ち、毎フレームの Update / Draw と削除を引き受けるクラス。
/// シーンのメンバーとして1つ持つ（例: GameObjectManager objects;）。
///
/// 1フレームの流れ（Update）:
///   1. 有効な物すべての Update() を呼ぶ
///   2. 物理と当たり判定。固定間隔（既定 1/60 秒）ごとに「Rigidbody を重力・速度で動かす → 判定と押し戻し」を
///      たまった時間の分だけ（0〜数回）行い、最後に OnCollisionEnter / Stay / Exit をまとめて1回通知する
///   3. Update 中に Create された物を本リストへ移す（次のフレームから動く）
///   4. Destroy() された物の Finalize() を呼んで取り除く
/// </summary>
class GameObjectManager {
public:
    GameObjectManager() = default;
    ~GameObjectManager();

    // コピー禁止（中身は unique_ptr なのでコピーできない。うっかり値渡しした時に分かりやすいエラーにする）
    GameObjectManager(const GameObjectManager&) = delete;
    GameObjectManager& operator=(const GameObjectManager&) = delete;

    /// <summary>
    /// 物を作って登録し、Initialize() まで済ませたポインタを返す。
    /// 引数は T のコンストラクタにそのまま渡される（例: Create<Enemy>(100.0f)）。
    /// Update中に作った物は次のフレームから動く（作ったフレームは Update されず、Draw だけされる。
    /// なので見た目の準備は Initialize で済ませておくとよい）。
    /// 返したポインタはマネージャーが持ち主なので delete しないこと（消すときは Destroy()）。
    /// </summary>
    template<class T, class... Args>
    T* Create(Args&&... args);

    // 毎フレーム1回、シーンの Update から呼ぶ
    void Update();
    // 毎フレーム1回、シーンの Draw から呼ぶ
    void Draw();
    // 影を描く（シーンが Object3dCommon::PreDrawShadow と PostDrawShadow の間で呼ぶ）。有効な物すべての DrawShadow を呼ぶ
    void DrawShadow(const Matrix4x4& lightViewProjection);

    // --- ModelObject の自動まとめ描き ---
    // Draw / DrawShadow の中で、同じモデルの ModelObject を1回の描画命令にまとめて描く（既定 true）。
    // 見た目は同じ。物ごとに外したいときは ModelObject::SetBatchingEnabled(false)
    void SetAutoBatching(bool enabled) { isAutoBatching = enabled; }
    bool IsAutoBatching() const { return isAutoBatching; }
    ModelBatcher& GetModelBatcher() { return modelBatcher; }
    const ModelBatcher& GetModelBatcher() const { return modelBatcher; }

    // 名前が一致する最初の物（見つからなければ nullptr）
    GameObject* Find(const std::string& name) const;
    // タグが一致する最初の物（見つからなければ nullptr）
    GameObject* FindByTag(const std::string& tag) const;
    // タグが一致する物すべて
    std::vector<GameObject*> FindAllByTag(const std::string& tag) const;
    // 型 T（またはその派生）の最初の物（例: FindByType<Player>()）。見つからなければ nullptr
    template<class T>
    T* FindByType() const;
    // 型 T（またはその派生）の物すべて（例: FindAllByType<Enemy>()）。非アクティブの物も含む
    template<class T>
    std::vector<T*> FindAllByType() const;

    // 全部の Finalize() を呼んで取り除く（シーンの Finalize で呼ぶ。デストラクタでも呼ばれる）
    void Clear();

    // 生きている物の数（Update中に作られた物を含み、Destroy済みの物は含まない）
    size_t GetCount() const;

    // 当たり判定の設定・レイキャスト用（例: GetCollisionManager().SetLayerCollision(1, 2, false)）
    CollisionManager& GetCollisionManager() { return collisionManager; }
    const CollisionManager& GetCollisionManager() const { return collisionManager; }

    // --- 物理（Rigidbody を付けた物だけが対象） ---
    // 重力（既定 {0, -9.8, 0} = 3D で 1 を 1m とした地球の重力）。
    // 2D（画面座標は y が下向き・単位がピクセル）では SetGravity({ 0.0f, 1500.0f, 0.0f }) のように下向きを + にする
    void SetGravity(const Vector3& newGravity) { gravity = newGravity; }
    const Vector3& GetGravity() const { return gravity; }
    // 物理を計算する間隔（既定 1/60 秒）。FPS が変わっても同じ動きになるよう、この間隔で必要な回数だけ計算する
    void SetFixedTimeStep(float seconds);
    float GetFixedTimeStep() const { return fixedTimeStep; }
    // 直前の Update で物理を何回計算したか（デバッグ表示用）
    int GetLastPhysicsStepCount() const { return lastPhysicsStepCount; }

    /// <summary>
    /// Draw の外で物を描く（例: シーンが自分でシャドウパスを回す）ときに、Rigidbody の補間位置を使わせるための関数。
    /// BeginRenderInterpolation() 〜 EndRenderInterpolation() の間は transform.translate が描画用の位置になる。
    /// Draw() は中で自動的に呼ぶので、Draw だけなら何もしなくてよい。必ず対で呼ぶこと
    /// </summary>
    void BeginRenderInterpolation();
    void EndRenderInterpolation();

private:
    // 物理の固定間隔ステップ（重力・速度で動かす → 当たり判定と押し戻し）をこのフレームの分だけ行う
    void UpdatePhysics(const std::vector<GameObject*>& alive);
    // Rigidbody 1つを step 秒ぶん動かす
    void IntegrateBody(Rigidbody& body, float step) const;
    // 回る物の向きを step 秒ぶん回す（transform.rotate にも書く）
    void IntegrateRotation(Rigidbody& body, float step) const;
    // 当たり判定に渡す一覧（本リストのうち Destroy されていない物。非アクティブも含む）
    std::vector<GameObject*> CollectAliveObjects() const;
    // Destroy() された物の Finalize() を呼んで取り除く
    void RemoveDestroyed();
    // Update 中に作られた物を本リストへ移す
    void MovePendingObjects();
    // 当たり判定の形と Rigidbody の速度を確認用の線で描く（DebugDraw::IsColliderVisible() のとき Draw から呼ぶ）
    void AddColliderDebugLines() const;
    // 検索用: Destroy されていない物すべてに func を呼び、func が true を返したらそこで止める
    template<class Func>
    void ForEachAlive(Func func) const;

    std::vector<std::unique_ptr<GameObject>> objects;
    // Update 中に Create された物の一時置き場（ループ中に objects を増やすとループが壊れるため）
    std::vector<std::unique_ptr<GameObject>> pendingObjects;
    // Update のループ中かどうか
    bool isUpdating = false;
    // 当たり判定（objects より後に宣言しているので、破棄は objects より先。どちらもデストラクタの Clear 後なので空）
    CollisionManager collisionManager;

    // 物理
    Vector3 gravity = { 0.0f, -9.8f, 0.0f };
    float fixedTimeStep = 1.0f / 60.0f;
    // まだ計算していない時間（固定間隔に満たない端数）
    float physicsAccumulator = 0.0f;
    int lastPhysicsStepCount = 0;
    // BeginRenderInterpolation 中に入れ替えた物と、元の位置・回転
    struct SavedTransform {
        GameObject* object = nullptr;
        Vector3 translate;
        Vector3 rotate;
    };
    std::vector<SavedTransform> savedPhysicsPositions;
    // BeginRenderInterpolation の入れ子の深さ（シーンの Begin〜End の中で Draw が呼ばれても二重に入れ替えない）
    int renderInterpolationDepth = 0;

    // ModelObject の自動まとめ描き
    ModelBatcher modelBatcher;
    bool isAutoBatching = true;
};

template<class T, class... Args>
T* GameObjectManager::Create(Args&&... args) {
    static_assert(std::is_base_of_v<GameObject, T>, "Create<T>() の T は GameObject を継承したクラスにしてください");

    std::unique_ptr<T> object = std::make_unique<T>(std::forward<Args>(args)...);
    T* result = object.get();
    result->manager = this;
    result->Initialize();

    if (isUpdating) {
        pendingObjects.push_back(std::move(object));
    } else {
        objects.push_back(std::move(object));
    }
    return result;
}

template<class T>
T* GameObjectManager::FindByType() const {
    T* found = nullptr;
    ForEachAlive([&found](GameObject* object) {
        found = dynamic_cast<T*>(object);
        return found != nullptr;
    });
    return found;
}

template<class T>
std::vector<T*> GameObjectManager::FindAllByType() const {
    std::vector<T*> found;
    ForEachAlive([&found](GameObject* object) {
        if (T* typed = dynamic_cast<T*>(object)) {
            found.push_back(typed);
        }
        return false; // 最後まで探す
    });
    return found;
}

template<class Func>
void GameObjectManager::ForEachAlive(Func func) const {
    for (const std::vector<std::unique_ptr<GameObject>>* list : { &objects, &pendingObjects }) {
        for (const std::unique_ptr<GameObject>& object : *list) {
            if (object->IsDestroyed()) {
                continue;
            }
            if (func(object.get())) {
                return;
            }
        }
    }
}
