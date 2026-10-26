#pragma once
#include <cstdint>

/// <summary>
/// SpriteCommon の描画キューに積める物（Sprite・TileMap のレイヤーなど）。
/// SpriteCommon::Enqueue で積むと、Flush のときにレイヤー順で RecordDrawCommand が呼ばれる。
/// 描くときは SpriteCommon の PSO・ルートシグネチャ（b0 SpriteMaterial / b1 WVP / t0 テクスチャ）が設定済み。
/// </summary>
class SpriteDrawable {
public:
    virtual ~SpriteDrawable() = default;
    // 描画レイヤー（大きいほど手前）
    virtual int GetLayer() const = 0;
    // 実際に描画コマンドを積む（SpriteCommon::Flush から呼ばれる）
    virtual void RecordDrawCommand(uint32_t srvIndex) = 0;
};
