#pragma once
#include "GameObjectManager.h"
#include "TiledData.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

class Sprite;

/// <summary>
/// Tiled のマップ（TiledLoader で読んだ物）から GameObject を作るクラス。シーンのメンバーとして持つ。
/// タイルの見た目は TileMap が描く（このクラスは物と当たり判定だけを作る）。
///
/// オブジェクトレイヤーの物をどのクラスで作るか（LevelSpawner と同じ考え方）:
///   1. Tiled の Class（古い Tiled では Type）が RegisterType した名前なら、そのクラス
///   2. 無ければ名前の "." より前（"Coin.002" → "Coin"）が RegisterType した名前なら、そのクラス
///   3. タイルを置いた物で、そのタイルにタイルセットで Class を付けていれば、その名前
///   4. どれも未登録のタイルの物は TiledTileObject（タイルの絵を出すだけの置物）。それ以外は作らない（ログに1回出る）
///
/// 作った物に自動で設定するもの（Initialize の後、OnLevelLoad の前）:
///   - transform.translate = 物の中心（x, y。ピクセル）、transform.rotate.z = Tiled の回転（ラジアン）
///   - 名前（Initialize で SetName していなければ Tiled の名前）
///   - 独自の項目 "collider" が "static" / "trigger" / "dynamic" で、Initialize でコライダーを付けていなければ 2D のコライダー
///     （四角・タイル・多角形は外枠の四角、楕円は円）
///   - OnLevelLoad(data): data.type = Class、data.name、data.raw = Tiled の物の JSON に独自の項目を "名前": 値 で足した物
///     （例: score = int(data.raw["score"].AsNumber())。Blender のレベルデータと同じ書き方で読める）
///
/// 壁の当たり判定（SpawnTileColliders）:
///   独自の項目 "collision" が true のタイルレイヤーのタイルと、タイルセットで "collision" を true にしたタイルが壁になる。
///   隣り合うタイルは大きな四角1つにまとめる（kStatic、名前 "TileCollider"、タグはレイヤーの独自の項目 "tag" があればそれ）。
///
/// 使い方:
///   TiledSpawner spawner;                         // シーンのメンバー
///   spawner.RegisterType<Player>("Player");       // Initialize で
///   spawner.LoadAndSpawn(objects, "Resources/maps/stage1.tmj");
///   tileMap.Initialize(spawner.GetMap());         // 見た目（TileMap）
///   spawner.Reload(objects);                      // 読み直し（成功したら tileMap も作り直す）
/// </summary>
class TiledSpawner {
public:
    template<class T>
    void RegisterType(const std::string& className);

    // map のオブジェクトレイヤーの物と、壁の当たり判定を作る。作った物を返す
    std::vector<GameObject*> Spawn(GameObjectManager& objects, const TiledMap& map);
    // filePath を読んで Spawn する（読み込みに失敗したら止まる）
    std::vector<GameObject*> LoadAndSpawn(GameObjectManager& objects, const std::string& filePath);
    // このスポナーが作った物を全部消して、最後に Spawn したマップを読み直して作り直す（失敗したら false。今の物は残る）
    bool Reload(GameObjectManager& objects, std::string* errorMessage = nullptr);
    // このスポナーが作った物を全部 Destroy する（実際に消えるのはフレームの最後）
    void DestroySpawned(GameObjectManager& objects) const;

    // 最後に Spawn したマップ（TileMap に渡す）
    const TiledMap& GetMap() const { return map; }
    // タイルの物（TiledTileObject）の描画レイヤーの基準（TileMap と同じ値にする。レイヤー i は baseLayer + i）
    void SetBaseLayer(int layer) { baseLayer = layer; }

private:
    using Factory = std::function<GameObject*(GameObjectManager&)>;

    // object に合うクラスで作る（作らないなら nullptr）
    GameObject* CreateObject(GameObjectManager& objects, const TiledObject& object, std::string& className);
    void SpawnObject(GameObjectManager& objects, const TiledObject& object, int layerIndex, std::vector<GameObject*>& spawned);
    // "collider" の独自の項目からコライダーを付ける
    static void AddObjectCollider(GameObject& gameObject, const TiledObject& object);
    void SpawnTileColliders(GameObjectManager& objects, std::vector<GameObject*>& spawned);
    // タイル1つが壁か（レイヤーの "collision" か、タイルセットのタイルの "collision"）
    bool IsSolidTile(const TiledLayer& layer, uint32_t gid) const;

    std::map<std::string, Factory> factories;
    TiledMap map;
    int baseLayer = 0;
    std::set<std::string> warnedNames;
};

/// <summary>
/// タイルの絵を1つ出すだけの置物（Tiled でタイルを置いた物のうち、クラスを登録していない物）。2D カメラに付いて動く。
/// 登録したクラスでタイルの絵を出したいときも、このクラスを継承すれば OnLevelLoad で絵が設定される。
/// </summary>
class TiledTileObject : public GameObject {
public:
    TiledTileObject();
    ~TiledTileObject() override;

    void Update() override;
    void Draw() override;
    // data.raw["gid"] のタイルの絵を出す（TiledSpawner が呼ぶ。継承したら TiledTileObject::OnLevelLoad も呼ぶこと）
    void OnLevelLoad(const LevelObjectData& data) override;

    // TiledSpawner が OnLevelLoad の前に渡す（絵の元のマップ・描画レイヤー・大きさ）
    void SetSource(const TiledMap* sourceMap, int layer, const Vector2& size) {
        map = sourceMap;
        drawLayer = layer;
        drawSize = size;
    }

private:
    std::unique_ptr<Sprite> sprite;
    const TiledMap* map = nullptr;
    int drawLayer = 0;
    Vector2 drawSize = { 0.0f, 0.0f };
};

template<class T>
void TiledSpawner::RegisterType(const std::string& className) {
    static_assert(std::is_base_of_v<GameObject, T>, "RegisterType<T>() の T は GameObject を継承したクラスにしてください");
    factories[className] = [](GameObjectManager& objects) -> GameObject* { return objects.Create<T>(); };
}
