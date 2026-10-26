#include "LevelSpawner.h"
#include "Calculation.h"
#include "LevelLoader.h"
#include "Logger.h"
#include "ModelManager.h"
#include "ModelObject.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

Vector3 MultiplyEach(const Vector3& a, const Vector3& b) {
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}

Vector3 AbsEach(const Vector3& v) {
    return { std::abs(v.x), std::abs(v.y), std::abs(v.z) };
}

// 親のワールド transform と子の相対 transform を合成する（回転は足し算の近似。LevelSpawner.h の「制限」参照）
Transform Combine(const Transform& parentWorld, const Transform& childLocal) {
    Matrix4x4 parentMatrix = Calculation::MakeAffineMatrix(parentWorld.scale, parentWorld.rotate, parentWorld.translate);
    Transform world;
    world.scale = MultiplyEach(parentWorld.scale, childLocal.scale);
    world.rotate = parentWorld.rotate + childLocal.rotate;
    world.translate = Calculation::Transform(childLocal.translate, parentMatrix);
    return world;
}

} // namespace

std::vector<GameObject*> LevelSpawner::Spawn(GameObjectManager& objects, const LevelData& level) {
    if (!level.filePath.empty()) {
        lastFilePath = level.filePath;
    }
    std::vector<GameObject*> spawned;
    for (const LevelObjectData& data : level.objects) {
        SpawnRecursive(objects, data, nullptr, spawned);
    }
    return spawned;
}

std::vector<GameObject*> LevelSpawner::LoadAndSpawn(GameObjectManager& objects, const std::string& filePath) {
    return Spawn(objects, LevelLoader::Load(filePath));
}

bool LevelSpawner::Reload(GameObjectManager& objects, std::string* errorMessage) {
    if (lastFilePath.empty()) {
        if (errorMessage) {
            *errorMessage = "まだファイルから Spawn していません";
        }
        return false;
    }
    // 先に読み込んでから消す（失敗したときに今の配置を残すため）
    LevelData level;
    if (!LevelLoader::TryLoad(lastFilePath, level, errorMessage)) {
        return false;
    }
    DestroySpawned(objects);
    Spawn(objects, level);
    return true;
}

void LevelSpawner::DestroySpawned(GameObjectManager& objects) const {
    // マネージャーに生きている物から探すので、ゲーム側で既に消された物には触らない
    for (GameObject* object : objects.FindAllByType<GameObject>()) {
        if (object->spawnedBy == this) {
            object->Destroy();
        }
    }
}

void LevelSpawner::SpawnRecursive(GameObjectManager& objects, const LevelObjectData& data, const Transform* parentWorld,
                                  std::vector<GameObject*>& spawned) {
    if (data.disabled) {
        return;
    }
    const Transform world = parentWorld ? Combine(*parentWorld, data.transform) : data.transform;

    bool isRegistered = false;
    if (GameObject* object = CreateObject(objects, data, isRegistered)) {
        object->spawnedBy = this;
        object->transform = world;
        if (object->GetName().empty()) {
            object->SetName(data.name);
        }
        if (data.collider.type != LevelColliderType::kNone && object->GetColliders().empty()) {
            AddLevelCollider(*object, data.collider, isRegistered);
        }
        // コライダーを付けた後に決める（仮の見た目を、クラス側で付けた分も含めた判定の形に合わせるため）
        if (auto* modelObject = dynamic_cast<ModelObject*>(object); modelObject && !modelObject->GetModel()) {
            const Collider* collider = object->GetCollider();
            const bool isSphere = collider && collider->GetShape() == ColliderShape::kSphere;
            modelObject->SetModel(LoadModelOrFallback(data.fileName, isSphere));
        }
        object->OnLevelLoad(data);
        spawned.push_back(object);
    }

    for (const LevelObjectData& child : data.children) {
        SpawnRecursive(objects, child, &world, spawned);
    }
}

GameObject* LevelSpawner::CreateObject(GameObjectManager& objects, const LevelObjectData& data, bool& isRegistered) {
    const JsonValue& classValue = data.raw["class"];
    const std::string className = classValue.IsString() ? classValue.AsString() : std::string();
    // Blender で複製すると付く ".001" などを取った名前（"Enemy.001" → "Enemy"）
    const std::string baseName = data.name.substr(0, data.name.find('.'));
    for (const std::string& key : { className, data.type, baseName }) {
        if (key.empty()) {
            continue;
        }
        if (auto it = factories.find(key); it != factories.end()) {
            if (key != className && warnedNames.insert("by:" + key).second) {
                Logger::Log("LevelSpawner: \"" + data.name + "\" は \"class\" が無いので、名前・種類から " + key + " で作ります\n");
            }
            isRegistered = true;
            return it->second(objects);
        }
    }

    isRegistered = false;
    if (!className.empty() && warnedNames.insert("class:" + className).second) {
        Logger::Log("LevelSpawner: class \"" + className + "\" は RegisterType されていません（置物として扱います）\n");
    }
    if (data.type == "MESH" || !data.fileName.empty()) {
        return objects.Create<ModelObject>();
    }
    if (warnedNames.insert("type:" + data.type).second) {
        Logger::Log("LevelSpawner: type \"" + data.type + "\" の物は作りません（" + data.name + " など）\n");
    }
    return nullptr;
}

void LevelSpawner::AddLevelCollider(GameObject& object, const LevelColliderData& collider, bool isRegistered) const {
    // コライダーは scale を掛けないので、ここでワールドの大きさにしておく
    const Vector3 scale = AbsEach(object.transform.scale);
    std::unique_ptr<Collider> shape;
    if (collider.type == LevelColliderType::kBox) {
        const Vector3 halfSize = MultiplyEach(collider.size, scale) * 0.5f;
        const Vector3& rotate = object.transform.rotate;
        const bool isRotated = rotate.x != 0.0f || rotate.y != 0.0f || rotate.z != 0.0f;
        // 回転していれば持ち主と一緒に回る箱（OBB）、していなければ AABB（こちらの方が軽く、カプセルとも当たる）
        shape = isRotated ? Collider::MakeOrientedBox(halfSize) : Collider::MakeBox(halfSize);
    } else {
        // (std::max) とかっこで囲むのは、windows.h の max マクロに置き換えられないようにするため
        shape = Collider::MakeSphere(collider.radius * (std::max)({ scale.x, scale.y, scale.z }));
    }
    Collider* added = object.AddCollider(std::move(shape));
    added->SetOffset(MultiplyEach(collider.center, object.transform.scale));
    if (!isRegistered) {
        added->SetType(ColliderType::kStatic);
    }
}

std::shared_ptr<Model> LevelSpawner::LoadModelOrFallback(const std::string& fileName, bool isSphere) {
    ModelManager* modelManager = ModelManager::GetInstance();
    if (fileName.empty()) {
        // モデル指定なし: 判定の形に合わせた仮の見た目（どちらも大きさ1なので scale がそのまま見た目の大きさ）
        return isSphere ? modelManager->GetSphereModel() : modelManager->GetCubeModel();
    }

    // "cube" → <フォルダ>/cube/cube.obj、"cube.obj" → <フォルダ>/cube.obj
    const bool hasExtension = std::filesystem::path(fileName).has_extension();
    const std::string directory = hasExtension ? modelDirectory : modelDirectory + "/" + fileName;
    const std::string file = hasExtension ? fileName : fileName + ".obj";
    if (!std::filesystem::exists(directory + "/" + file)) {
        if (warnedNames.insert("model:" + fileName).second) {
            Logger::Log("LevelSpawner: モデルが見つからないので白い立方体で表示します: " + directory + "/" + file + "\n");
        }
        return modelManager->GetCubeModel();
    }
    return modelManager->LoadModel(directory, file);
}
