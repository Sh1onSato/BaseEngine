#pragma once
#include "SpriteDrawable.h"
#include "Structs.h"
#include "TextureManager.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <wrl.h>

struct TiledMap;
class SpriteCommon;

/// <summary>
/// Tiled のマップ（TiledLoader で読んだ物）のタイルレイヤーを描くクラス。
/// レイヤー × タイルセットごとに、全部のタイルを1回の描画命令で描く（Sprite を何千個も作らない）。
/// Sprite と同じく SpriteCommon の描画キューに積むので、レイヤー順・2D カメラ（Camera2D）がそのまま効く。
///
/// 使い方:
///   TileMap tileMap;                         // シーンのメンバー
///   tileMap.Initialize(spawner.GetMap());    // マップを読んだ後（読み直したらもう一度呼ぶ）
///   tileMap.Draw();                          // シーンの Draw で（Sprite::Draw と同じく積むだけ）
/// 描画レイヤーは Tiled のレイヤーの順番（下から i 番目なら baseLayer + i）。キャラクターの Sprite を
/// 背景と前景の間に出したいときは、その間の数を SetLayer する（TiledSpawner::SetBaseLayer と同じ値にそろえる）。
/// 見えないレイヤー（Tiled で目のアイコンを消した物）と、独自の項目 "hidden" が true のレイヤーは描かない（当たり判定だけのレイヤー用）。
/// </summary>
class TileMap {
public:
    TileMap();
    ~TileMap();

    // map のタイルレイヤーを描けるようにする（タイルセットの画像も読み込む）
    void Initialize(const TiledMap& map);
    // 描画キューに積む（毎フレーム、シーンの Draw で呼ぶ）
    void Draw();

    // 描画レイヤーの基準（既定 0。Initialize の前に設定する）
    void SetBaseLayer(int layer) { baseLayer = layer; }
    // Camera2D に付いて動くか（既定 true）
    void SetUseCamera(bool use) { useCamera = use; }
    // 描画命令の数（デバッグ表示用）
    size_t GetDrawCallCount() const { return batches.size(); }

private:
    // レイヤー1つ × タイルセット1つ分のタイル
    class Batch : public SpriteDrawable {
    public:
        Batch(SpriteCommon* spriteCommon, const TileMap* owner, int layer);
        // vertices を GPU に送り、画像と濃さを設定する
        void Build(const std::vector<SpriteVertex>& vertices, const std::string& imagePath, float opacity);
        int GetLayer() const override { return layer; }
        void RecordDrawCommand(uint32_t srvIndex) override;
        uint32_t GetSrvIndex() const;

    private:
        SpriteCommon* spriteCommon = nullptr;
        const TileMap* owner = nullptr;
        int layer = 0;
        UINT vertexCount = 0;
        TextureHandle texture;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> transformResource;
        TransformationMatrix* transformData = nullptr;
    };

    std::vector<std::unique_ptr<Batch>> batches;
    int baseLayer = 0;
    bool useCamera = true;
};
