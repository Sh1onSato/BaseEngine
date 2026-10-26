#include "TiledSpawner.h"
#include "Framework.h"
#include "LevelData.h"
#include "Logger.h"
#include "Sprite.h"
#include "TextureManager.h"
#include "TiledLoader.h"
#include <algorithm>
#include <numbers>

namespace {
    constexpr float kDegreeToRadian = std::numbers::pi_v<float> / 180.0f;

    bool GetBoolProperty(const JsonValue& properties, const char* name) {
        return properties[name].IsBool() && properties[name].AsBool();
    }

    // 多角形・折れ線の頂点を囲む四角の大きさ
    Vector2 GetPointsSize(const std::vector<Vector2>& points) {
        if (points.empty()) {
            return { 0.0f, 0.0f };
        }
        Vector2 minPoint = points.front();
        Vector2 maxPoint = points.front();
        for (const Vector2& point : points) {
            minPoint = { (std::min)(minPoint.x, point.x), (std::min)(minPoint.y, point.y) };
            maxPoint = { (std::max)(maxPoint.x, point.x), (std::max)(maxPoint.y, point.y) };
        }
        return { maxPoint.x - minPoint.x, maxPoint.y - minPoint.y };
    }
}

// ===== TiledSpawner =====

std::vector<GameObject*> TiledSpawner::Spawn(GameObjectManager& objects, const TiledMap& newMap) {
    // TileMap・Reload・タイルの物が使うので、自分の手元に写しておく
    if (&newMap != &map) {
        map = newMap;
    }
    std::vector<GameObject*> spawned;
    for (size_t i = 0; i < map.layers.size(); ++i) {
        if (map.layers[i].type != TiledLayerType::kObject) {
            continue;
        }
        for (const TiledObject& object : map.layers[i].objects) {
            SpawnObject(objects, object, static_cast<int>(i), spawned);
        }
    }
    SpawnTileColliders(objects, spawned);
    return spawned;
}

std::vector<GameObject*> TiledSpawner::LoadAndSpawn(GameObjectManager& objects, const std::string& filePath) {
    return Spawn(objects, TiledLoader::Load(filePath));
}

bool TiledSpawner::Reload(GameObjectManager& objects, std::string* errorMessage) {
    if (map.filePath.empty()) {
        if (errorMessage) {
            *errorMessage = "まだファイルから Spawn していません";
        }
        return false;
    }
    // 先に読み込んでから消す（失敗したときに今の配置を残すため）
    TiledMap newMap;
    if (!TiledLoader::TryLoad(map.filePath, newMap, errorMessage)) {
        return false;
    }
    DestroySpawned(objects);
    Spawn(objects, newMap);
    return true;
}

void TiledSpawner::DestroySpawned(GameObjectManager& objects) const {
    for (GameObject* object : objects.FindAllByType<GameObject>()) {
        if (object->spawnedBy == this) {
            object->Destroy();
        }
    }
}

GameObject* TiledSpawner::CreateObject(GameObjectManager& objects, const TiledObject& object, std::string& className) {
    auto tryCreate = [&](const std::string& name) -> GameObject* {
        auto it = factories.find(name);
        if (name.empty() || it == factories.end()) {
            return nullptr;
        }
        className = name;
        return it->second(objects);
    };
    // 1. Class  2. 名前の "." より前  3. タイルセットでそのタイルに付けた Class
    if (GameObject* created = tryCreate(object.className)) {
        return created;
    }
    if (GameObject* created = tryCreate(object.name.substr(0, object.name.find('.')))) {
        return created;
    }
    if (object.shape == TiledShape::kTile) {
        if (const TiledTileset* tileset = map.FindTileset(object.gid)) {
            const TiledTileInfo* info = tileset->FindTileInfo(TiledGid::GetId(object.gid) - tileset->firstGid);
            if (info) {
                if (GameObject* created = tryCreate(info->className)) {
                    return created;
                }
            }
        }
        // 4. 未登録のタイルの物は、絵を出すだけの置物
        className = object.className;
        return objects.Create<TiledTileObject>();
    }
    const std::string key = object.className.empty() ? object.name : object.className;
    if (warnedNames.insert(key).second) {
        Logger::Log("TiledSpawner: 登録されていないクラスなので作りません（RegisterType すると作られます）: " + key + "\n");
    }
    return nullptr;
}

void TiledSpawner::SpawnObject(GameObjectManager& objects, const TiledObject& object, int layerIndex, std::vector<GameObject*>& spawned) {
    std::string className;
    GameObject* gameObject = CreateObject(objects, object, className);
    if (!gameObject) {
        return;
    }
    gameObject->spawnedBy = this;
    const Vector2 center = object.GetCenter();
    gameObject->transform.translate = { center.x, center.y, 0.0f };
    gameObject->transform.rotate = { 0.0f, 0.0f, object.rotation * kDegreeToRadian };
    if (gameObject->GetName().empty()) {
        gameObject->SetName(object.name);
    }
    if (gameObject->GetColliders().empty()) {
        AddObjectCollider(*gameObject, object);
    }
    if (auto* tileObject = dynamic_cast<TiledTileObject*>(gameObject)) {
        tileObject->SetSource(&map, baseLayer + layerIndex, { object.width, object.height });
    }

    LevelObjectData data;
    data.type = className;
    data.name = object.name;
    data.transform = gameObject->transform;
    data.raw = object.raw;
    // タイルの物は、タイルセットでそのタイルに付けた独自の項目も持つ（Tiled と同じ。物に付けた値が優先）
    if (object.shape == TiledShape::kTile) {
        if (const TiledTileset* tileset = map.FindTileset(object.gid)) {
            if (const TiledTileInfo* info = tileset->FindTileInfo(TiledGid::GetId(object.gid) - tileset->firstGid)) {
                JsonValue::Object merged = object.raw.AsObject();
                for (const auto& entry : info->properties.AsObject()) {
                    merged.push_back(entry);
                }
                data.raw = JsonValue(std::move(merged));
            }
        }
    }
    gameObject->OnLevelLoad(data);
    spawned.push_back(gameObject);
}

void TiledSpawner::AddObjectCollider(GameObject& gameObject, const TiledObject& object) {
    const JsonValue& value = object.raw["collider"];
    if (!value.IsString()) {
        return;
    }
    const std::string& typeName = value.AsString();
    ColliderType type = ColliderType::kStatic;
    if (typeName == "trigger") {
        type = ColliderType::kTrigger;
    } else if (typeName == "dynamic") {
        type = ColliderType::kDynamic;
    } else if (typeName != "static") {
        Logger::Log("TiledSpawner: \"collider\" は static / trigger / dynamic のどれかにしてください: " + object.name + "\n");
        return;
    }

    std::unique_ptr<Collider> collider;
    switch (object.shape) {
    case TiledShape::kEllipse:
        collider = Collider::MakeCircle((std::max)(object.width, object.height) * 0.5f);
        break;
    case TiledShape::kPolygon:
    case TiledShape::kPolyline:
        collider = Collider::MakeRect(GetPointsSize(object.points));
        break;
    case TiledShape::kPoint:
        Logger::Log("TiledSpawner: 点の物にはコライダーを付けられません: " + object.name + "\n");
        return;
    default:
        collider = Collider::MakeRect({ object.width, object.height });
        break;
    }
    gameObject.AddCollider(std::move(collider))->SetType(type);
}

bool TiledSpawner::IsSolidTile(const TiledLayer& layer, uint32_t gid) const {
    const uint32_t id = TiledGid::GetId(gid);
    if (id == 0) {
        return false;
    }
    if (GetBoolProperty(layer.properties, "collision")) {
        return true;
    }
    const TiledTileset* tileset = map.FindTileset(gid);
    const TiledTileInfo* info = tileset ? tileset->FindTileInfo(id - tileset->firstGid) : nullptr;
    return info && GetBoolProperty(info->properties, "collision");
}

void TiledSpawner::SpawnTileColliders(GameObjectManager& objects, std::vector<GameObject*>& spawned) {
    const float tileWidth = static_cast<float>(map.tileWidth);
    const float tileHeight = static_cast<float>(map.tileHeight);
    for (const TiledLayer& layer : map.layers) {
        if (layer.type != TiledLayerType::kTile) {
            continue;
        }
        const int width = layer.width;
        const int height = layer.height;
        std::vector<bool> isSolid(layer.tiles.size());
        for (size_t i = 0; i < layer.tiles.size(); ++i) {
            isSolid[i] = IsSolidTile(layer, layer.tiles[i]);
        }
        // 左上から、まだ使っていない壁のタイルを見つけたら、右へ・下へできるだけ広げた四角を1つ作る
        std::vector<bool> isUsed(layer.tiles.size(), false);
        auto isFree = [&](int x, int y) { const size_t i = static_cast<size_t>(y * width + x); return isSolid[i] && !isUsed[i]; };
        const std::string& tag = layer.properties["tag"].IsString() ? layer.properties["tag"].AsString() : std::string();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!isFree(x, y)) continue;
                int runWidth = 1;
                while (x + runWidth < width && isFree(x + runWidth, y)) ++runWidth;
                int runHeight = 1;
                for (bool canGrow = true; canGrow && y + runHeight < height;) {
                    for (int dx = 0; dx < runWidth; ++dx) {
                        if (!isFree(x + dx, y + runHeight)) { canGrow = false; break; }
                    }
                    if (canGrow) ++runHeight;
                }
                for (int dy = 0; dy < runHeight; ++dy) {
                    for (int dx = 0; dx < runWidth; ++dx) {
                        isUsed[static_cast<size_t>((y + dy) * width + x + dx)] = true;
                    }
                }

                GameObject* wall = objects.Create<GameObject>();
                wall->spawnedBy = this;
                wall->SetName("TileCollider");
                if (!tag.empty()) {
                    wall->SetTag(tag);
                }
                wall->transform.translate = { layer.offset.x + (x + runWidth * 0.5f) * tileWidth,
                                              layer.offset.y + (y + runHeight * 0.5f) * tileHeight, 0.0f };
                wall->AddCollider(Collider::MakeRect({ runWidth * tileWidth, runHeight * tileHeight }))->SetType(ColliderType::kStatic);
                spawned.push_back(wall);
            }
        }
    }
}

// ===== TiledTileObject =====

TiledTileObject::TiledTileObject() = default;
// Sprite の中身はヘッダーで見えないので、デストラクタは .cpp に置く（unique_ptr<Sprite> の破棄に必要）
TiledTileObject::~TiledTileObject() = default;

void TiledTileObject::OnLevelLoad(const LevelObjectData& data) {
    if (!map || !data.raw["gid"].IsNumber()) {
        return;
    }
    const uint32_t gid = static_cast<uint32_t>(data.raw["gid"].AsNumber());
    const TiledTileset* tileset = map->FindTileset(gid);
    if (!tileset || tileset->imagePath.empty()) {
        return;
    }
    const uint32_t localId = TiledGid::GetId(gid) - tileset->firstGid;
    const Vector2 tileSize = { static_cast<float>(tileset->tileWidth), static_cast<float>(tileset->tileHeight) };

    TextureManager::GetInstance()->LoadTexture(tileset->imagePath);
    sprite = std::make_unique<Sprite>();
    sprite->Initialize(Framework::GetInstance()->GetSpriteCommon(), tileset->imagePath);
    sprite->SetAnchorPoint({ 0.5f, 0.5f });
    sprite->SetTexLeftTop(tileset->GetTileOrigin(localId));
    sprite->SetTexSize(tileSize);
    sprite->SetSize(drawSize.x > 0.0f && drawSize.y > 0.0f ? drawSize : tileSize);
    sprite->SetIsFlipX((gid & TiledGid::kFlipHorizontal) != 0);
    sprite->SetIsFlipY((gid & TiledGid::kFlipVertical) != 0);
    // マップと一緒にスクロールする・ドット絵がにじまないようにする
    sprite->SetUseCamera(true);
    sprite->SetTextureFilter(Sprite::TextureFilter::kPoint);
    sprite->SetLayer(drawLayer);
}

void TiledTileObject::Update() {
    if (!sprite) {
        return;
    }
    sprite->SetPosition({ transform.translate.x, transform.translate.y });
    sprite->SetRotation(transform.rotate.z);
    sprite->Update();
}

void TiledTileObject::Draw() {
    if (sprite) {
        sprite->Draw();
    }
}
