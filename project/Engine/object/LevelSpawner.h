#pragma once
#include "GameObjectManager.h"
#include "LevelData.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

class Model;

/// <summary>
/// LevelData（LevelLoader で読んだ配置）から GameObject を作るクラス。シーンのメンバーとして持つ。
///
/// どのクラスで作るか:
///   1. JSON の "class"（Blender のカスタムプロパティ）が RegisterType した名前なら、そのクラス
///   2. 無ければ "type"（"MESH" など）が RegisterType した名前なら、そのクラス
///   3. 無ければ Blender の名前の "." より前（"Enemy.001" → "Enemy"）が RegisterType した名前なら、そのクラス
///      （アドオンが "class" を書き出せないときの予備。どれで決まったかはログに1回出る）
///   4. どれも未登録で "MESH" か "file_name" 付きなら ModelObject（モデルを表示するだけの置物）
///   5. それ以外（"CAMERA" "LIGHT" など）は作らない（子は作る）。"disabled": true の物は子ごと作らない
///
/// 作った物に自動で設定するもの（Initialize の後、OnLevelLoad の前）:
///   - transform（子は親と合成したワールドの値）、名前（Initialize で SetName していなければ Blender の名前）
///   - ModelObject（とその派生）でモデル未設定なら "file_name" のモデル。見つからなければ白い立方体＋ログ
///   - "collider" があり、Initialize でコライダーを付けていなければそのコライダー。
///     大きさは transform.scale を掛けた値（Blender で見た目と一緒に拡大した箱がそのまま判定になる）。
///     BOX は回転していなければ AABB、回転していれば OBB（※カプセル×OBB は未対応）。
///     未登録の置物は kStatic（壁・床）、RegisterType したクラスは既定の kDynamic（OnLevelLoad で変えられる）
///
/// 使い方:
///   LevelSpawner spawner;                    // シーンのメンバー
///   spawner.RegisterType<Enemy>("Enemy");    // Initialize で（Enemy は引数なしで作れること）
///   spawner.LoadAndSpawn(objects, "Resources/levels/stage1.json");
///   spawner.Reload(objects);                 // 読み直し（Debug でキーに割り当てると配置調整が楽）
///
/// 制限: 親が回転している子の回転は「親の回転 + 子の回転」で近似している（同じ軸だけで回すなら正確。位置は常に正確）。
/// </summary>
class LevelSpawner {
public:
    // "class" または "type" が className の物を T で作るよう登録する
    template<class T>
    void RegisterType(const std::string& className);

    // モデルを探すフォルダ（既定 "Resources/models"）。
    // "file_name" が "cube" なら <フォルダ>/cube/cube.obj（無ければ cube.gltf → cube.glb）、"cube.obj" なら <フォルダ>/cube.obj を読む
    void SetModelDirectory(const std::string& directory) { modelDirectory = directory; }

    // level の物をすべて objects に作り、作った物を返す（Reload で読み直せるよう level.filePath を覚える）
    std::vector<GameObject*> Spawn(GameObjectManager& objects, const LevelData& level);
    // filePath を読み込んで Spawn する（読み込みに失敗したら止まる）
    std::vector<GameObject*> LoadAndSpawn(GameObjectManager& objects, const std::string& filePath);
    // このスポナーが作った物を全部消して、最後に Spawn したファイルを読み直して作り直す。
    // 読み込みに失敗したら false（今の物はそのまま残る。JSON の書きかけで落ちないように）。
    // 注意: モデル(.obj / .gltf / .glb)は ModelManager が覚えているので、中身を書き換えても読み直されない
    // （シーンを切り替えて誰も使わなくなれば解放されるので、別のシーンを挟んでから戻れば読み直される）
    bool Reload(GameObjectManager& objects, std::string* errorMessage = nullptr);
    // このスポナーが作った物を全部 Destroy する（実際に消えるのはフレームの最後）
    void DestroySpawned(GameObjectManager& objects) const;

private:
    using Factory = std::function<GameObject*(GameObjectManager&)>;

    // data と子を作る（parentWorld が nullptr ならルート）
    void SpawnRecursive(GameObjectManager& objects, const LevelObjectData& data, const Transform* parentWorld,
                        std::vector<GameObject*>& spawned);
    // data に合うクラスで作る（作らない種類なら nullptr）。isRegistered に登録済みクラスかどうかを入れる
    GameObject* CreateObject(GameObjectManager& objects, const LevelObjectData& data, bool& isRegistered);
    // "collider" をワールドの大きさにして付ける
    void AddLevelCollider(GameObject& object, const LevelColliderData& collider, bool isRegistered) const;
    // "file_name" のモデル（見つからなければ白い立方体。"file_name" が空なら isSphere で白い球か立方体）
    std::shared_ptr<Model> LoadModelOrFallback(const std::string& fileName, bool isSphere);

    std::map<std::string, Factory> factories;
    std::string modelDirectory = "Resources/models";
    // 最後に Spawn したファイル（Reload 用）
    std::string lastFilePath;
    // 同じ警告を何度も出さないための記録（作らない種類・見つからないモデル）
    std::set<std::string> warnedNames;
};

template<class T>
void LevelSpawner::RegisterType(const std::string& className) {
    static_assert(std::is_base_of_v<GameObject, T>, "RegisterType<T>() の T は GameObject を継承したクラスにしてください");
    factories[className] = [](GameObjectManager& objects) -> GameObject* { return objects.Create<T>(); };
}
