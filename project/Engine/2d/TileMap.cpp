#include "TileMap.h"
#include "Calculation.h"
#include "Camera2D.h"
#include "Framework.h"
#include "Logger.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include "TiledData.h"
#include <cstring>
#include <map>
#include <utility>

namespace {
    // 隣のタイルの色がにじまないよう、画像の切り出しをほんの少し内側にする（テクセル単位）
    constexpr float kUvInset = 0.01f;

    // 1枚のタイル（2つの三角形 = 6頂点）を足す。flip は Tiled の反転の印
    void AddTileQuad(std::vector<SpriteVertex>& vertices, const Vector2& position, const Vector2& size,
                     const Vector2& uvMin, const Vector2& uvMax, uint32_t gid) {
        // 角: 0 左上 / 1 右上 / 2 左下 / 3 右下 と、そこに貼る画像の位置
        Vector2 uv[4] = { { uvMin.x, uvMin.y }, { uvMax.x, uvMin.y }, { uvMin.x, uvMax.y }, { uvMax.x, uvMax.y } };
        // Tiled と同じ順番（斜め → 左右 → 上下）で反転する
        if (gid & TiledGid::kFlipDiagonal) { std::swap(uv[1], uv[2]); }
        if (gid & TiledGid::kFlipHorizontal) { std::swap(uv[0], uv[1]); std::swap(uv[2], uv[3]); }
        if (gid & TiledGid::kFlipVertical) { std::swap(uv[0], uv[2]); std::swap(uv[1], uv[3]); }
        const Vector4 corner[4] = {
            { position.x, position.y, 0.0f, 1.0f },
            { position.x + size.x, position.y, 0.0f, 1.0f },
            { position.x, position.y + size.y, 0.0f, 1.0f },
            { position.x + size.x, position.y + size.y, 0.0f, 1.0f },
        };
        // 時計回り（左上 → 右上 → 左下、右上 → 右下 → 左下）
        for (int index : { 0, 1, 2, 1, 3, 2 }) {
            vertices.push_back({ corner[index], uv[index] });
        }
    }
}

TileMap::TileMap() = default;
TileMap::~TileMap() = default;

void TileMap::Initialize(const TiledMap& map) {
    batches.clear();
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();
    for (size_t layerIndex = 0; layerIndex < map.layers.size(); ++layerIndex) {
        const TiledLayer& layer = map.layers[layerIndex];
        const bool isHidden = layer.properties["hidden"].IsBool() && layer.properties["hidden"].AsBool();
        if (layer.type != TiledLayerType::kTile || !layer.visible || isHidden) {
            continue;
        }
        // タイルセットごとに頂点を集める（1レイヤーに複数のタイルセットを混ぜてもよい）
        std::map<const TiledTileset*, std::vector<SpriteVertex>> verticesByTileset;
        for (int row = 0; row < layer.height; ++row) {
            for (int column = 0; column < layer.width; ++column) {
                const uint32_t gid = layer.tiles[static_cast<size_t>(row * layer.width + column)];
                const TiledTileset* tileset = map.FindTileset(gid);
                if (!tileset || tileset->imageWidth <= 0 || tileset->imageHeight <= 0) {
                    continue;
                }
                const uint32_t localId = TiledGid::GetId(gid) - tileset->firstGid;
                const Vector2 origin = tileset->GetTileOrigin(localId);
                const Vector2 imageSize = { static_cast<float>(tileset->imageWidth), static_cast<float>(tileset->imageHeight) };
                const Vector2 tileSize = { static_cast<float>(tileset->tileWidth), static_cast<float>(tileset->tileHeight) };
                const Vector2 uvMin = { (origin.x + kUvInset) / imageSize.x, (origin.y + kUvInset) / imageSize.y };
                const Vector2 uvMax = { (origin.x + tileSize.x - kUvInset) / imageSize.x, (origin.y + tileSize.y - kUvInset) / imageSize.y };
                // マップのマスより大きいタイルは、Tiled と同じくマスの左下にそろえる
                const Vector2 position = {
                    layer.offset.x + static_cast<float>(column * map.tileWidth),
                    layer.offset.y + static_cast<float>((row + 1) * map.tileHeight) - tileSize.y };
                AddTileQuad(verticesByTileset[tileset], position, tileSize, uvMin, uvMax, gid);
            }
        }
        for (const auto& [tileset, vertices] : verticesByTileset) {
            auto batch = std::make_unique<Batch>(spriteCommon, this, baseLayer + static_cast<int>(layerIndex));
            batch->Build(vertices, tileset->imagePath, layer.opacity);
            batches.push_back(std::move(batch));
        }
    }
}

void TileMap::Draw() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();
    for (const std::unique_ptr<Batch>& batch : batches) {
        spriteCommon->Enqueue(batch.get(), batch->GetSrvIndex());
    }
}

// ===== Batch =====

TileMap::Batch::Batch(SpriteCommon* spriteCommon, const TileMap* owner, int layer)
    : spriteCommon(spriteCommon), owner(owner), layer(layer) {}

void TileMap::Batch::Build(const std::vector<SpriteVertex>& vertices, const std::string& imagePath, float opacity) {
    DirectXCommon* dxCommon = spriteCommon->GetDxCommon();
    vertexCount = static_cast<UINT>(vertices.size());

    // 動かさないので、作るときに1回だけ書き込む
    vertexResource = dxCommon->CreatBufferResource(sizeof(SpriteVertex) * vertices.size());
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(SpriteVertex) * vertices.size());
    vertexBufferView.StrideInBytes = sizeof(SpriteVertex);
    SpriteVertex* vertexData = nullptr;
    Logger::CheckHResult(vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData)), "TileMap: Map vertexResource");
    std::memcpy(vertexData, vertices.data(), sizeof(SpriteVertex) * vertices.size());
    vertexResource->Unmap(0, nullptr);

    // 色（レイヤーの濃さ）・UV はそのまま・ドット絵がにじまないようポイント
    materialResource = dxCommon->CreatBufferResource(sizeof(SpriteMaterial));
    SpriteMaterial* material = nullptr;
    Logger::CheckHResult(materialResource->Map(0, nullptr, reinterpret_cast<void**>(&material)), "TileMap: Map materialResource");
    material->color = { 1.0f, 1.0f, 1.0f, opacity };
    material->uvTransform = Calculation::MakeIdentity4x4();
    material->usePointFilter = 1;
    materialResource->Unmap(0, nullptr);

    transformResource = dxCommon->CreatBufferResource(sizeof(TransformationMatrix));
    Logger::CheckHResult(transformResource->Map(0, nullptr, reinterpret_cast<void**>(&transformData)), "TileMap: Map transformResource");
    transformData->World = Calculation::MakeIdentity4x4();
    transformData->WVP = Calculation::MakeIdentity4x4();

    TextureManager::GetInstance()->LoadTexture(imagePath);
    texture = TextureManager::GetInstance()->Acquire(imagePath);
}

uint32_t TileMap::Batch::GetSrvIndex() const {
    return TextureManager::GetInstance()->GetSrvIndex(texture.GetIndex());
}

void TileMap::Batch::RecordDrawCommand(uint32_t srvIndex) {
    ID3D12GraphicsCommandList* commandList = spriteCommon->GetDxCommon()->GetCommandList();
    spriteCommon->SetBlendMode(SpriteBlendMode::kNormal);

    // Sprite と同じく、描く直前にカメラ込みの行列を計算する（頂点はワールド座標なので World は単位行列）
    Camera2D* camera = spriteCommon->GetCamera2D();
    const Matrix4x4 viewMatrix = (owner->useCamera && camera) ? camera->GetViewMatrix() : Calculation::MakeIdentity4x4();
    const Vector2 gameSize = Screen::GetGameSize();
    const Matrix4x4 projectionMatrix = Calculation::MakeOrthographicMatrix(0.0f, 0.0f, gameSize.x, gameSize.y, 0.0f, 100.0f);
    transformData->WVP = viewMatrix * projectionMatrix;

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, transformResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(2, spriteCommon->GetTextureHandle(srvIndex));
    commandList->DrawInstanced(vertexCount, 1, 0, 0);
}
