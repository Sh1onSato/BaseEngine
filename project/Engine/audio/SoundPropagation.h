#pragma once
#include "AudioManager.h"
#include "Structs.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

class NavGrid;

// 音の回り込みの細かい設定
struct SoundPropagationSettings {
    float maxDistance = 60.0f;              // 耳からこれより遠い道は探さない（その先は壁越しの扱い）
    float cornerOcclusion = 0.3f;           // 見通せない（角を曲がってくる）音のこもり具合の最小
    float detourForFullOcclusion = 20.0f;   // 真っすぐの距離よりこれだけ遠回りすると、こもり具合が 1 になる
    float throughWallOcclusion = 1.0f;      // 壁越しに聞こえる音のこもり具合
    float wallPenaltyDistance = 10.0f;      // 壁越しの音は、真っすぐの距離にこれを足した遠さで聞こえる（壁で小さくなる分）。
                                            // 回り込む道がこれより短ければ回り込み、長ければ壁越しに聞こえる
    float smoothingSpeed = 10.0f;           // Apply で聞こえる位置・こもり具合を追いかける速さ（急に飛ばないように）
};

// 1つの音がどう届くか（Evaluate の結果）
struct SoundPathResult {
    enum class Kind {
        kDirect,      // 見通せる（そのまま聞こえる）
        kAround,      // 扉などを回り込んで聞こえる
        kThroughWall, // 道が無い（壁越しにこもって聞こえる）
    };
    Kind kind = Kind::kDirect;
    Vector3 apparentPosition = { 0.0f, 0.0f, 0.0f }; // 聞こえる位置（回り込みなら、曲がり角の方向に道の長さだけ離れた所）
    Vector3 cornerPosition = { 0.0f, 0.0f, 0.0f };   // 回り込みのとき、耳から見える最後の曲がり角（扉）
    float pathDistance = 0.0f;   // 音が通ってくる道の長さ（回り込みなら遠回りの分だけ長い）
    float directDistance = 0.0f; // 真っすぐの距離
    float occlusion = 0.0f;      // こもり具合（0〜1）
};

/// <summary>
/// 音の回り込み。壁のある所では、音は扉や通路を回り込んで届く。耳から格子（NavGrid）の上で道を広げ、
/// 「扉の方から聞こえる」「遠回りの長さだけ小さく、こもって聞こえる」「道が無ければ壁越しにこもる」を求める。
///
/// 使い方:
///   NavGrid soundGrid;                                     // 経路探索と同じ格子でよいが、半径は小さく（扉を塞がないように）
///   soundGrid.Initialize(角, 幅, 奥行き, 0.5f, NavPlane::kXZ);
///   soundGrid.BlockColliders(objects, 0.1f);
///   propagation.SetGrid(&soundGrid);
///   // 毎フレーム（シーンの Update で、音の Apply より先に）
///   propagation.Update();                                   // 耳（AudioManager のリスナー）が別のマスに動いたときだけ道を作り直す
///   propagation.Apply(footstepHandle, enemyPosition);       // SetSoundPosition と SetOcclusion の代わり
/// 扉が開いた・閉じたなど、格子を変えたら Invalidate() を呼ぶ。
/// 道を求めるのは耳が別のマスに動いたときだけで、音の数が増えても1つ分の計算は軽い（表を引くだけ）。
/// 高さの違う床（2階・吹き抜け）は扱えない（格子が平らな1枚のため）。
/// </summary>
class SoundPropagation {
public:
    // 壁の格子（借りるだけ。SoundPropagation より長く生きていること）
    void SetGrid(const NavGrid* newGrid);
    void SetSettings(const SoundPropagationSettings& newSettings);
    const SoundPropagationSettings& GetSettings() const { return settings; }

    // 格子を変えたときに呼ぶ（次の Update で作り直す）
    void Invalidate() { isDirty = true; }

    // 毎フレーム呼ぶ。耳の位置は AudioManager のリスナー（カメラに付いていく・SetListener で決めた物）
    void Update();
    // 道を作り直すのにかかった時間（確認表示用）
    float GetLastBuildMilliseconds() const { return lastBuildMilliseconds; }

    // sourcePosition の音がどう届くかを求める
    SoundPathResult Evaluate(const Vector3& sourcePosition) const;
    // Evaluate して、鳴っている音の位置とこもり具合に入れる（急に飛ばないよう少しずつ追いかける）
    SoundPathResult Apply(SoundHandle handle, const Vector3& sourcePosition);

    // 音が通ってくる道を DebugDraw で描く（見通せる = 緑、回り込み = 黄、壁越し = 赤）
    void DrawDebug(const Vector3& sourcePosition) const;

private:
    void Rebuild(int listenerCell);
    // 音のある位置のマス（壁の中なら近くの歩けるマス）。見つからなければ -1
    int FindSourceCell(const Vector3& sourcePosition) const;
    // マスの中心のワールド座標を、高さ（格子の面の外の座標）だけ reference に合わせた物
    Vector3 CellToWorldAt(int cell, const Vector3& reference) const;

    const NavGrid* grid = nullptr;
    SoundPropagationSettings settings;
    bool isDirty = true;
    int builtListenerCell = -1;
    Vector3 listenerPosition = { 0.0f, 0.0f, 0.0f };
    float lastBuildMilliseconds = 0.0f;

    // マスごとの道の情報（Rebuild で作る）
    struct Cell {
        float distance = -1.0f; // 耳のマスからの格子の上の距離（マスの数）。-1 なら届かない
        int parent = -1;        // 耳の方へ1つ戻ったマス
        int corner = -1;        // 耳から見える最後のマス（見通せるマスなら自分）
        bool isVisible = false; // 耳のマスから見通せる
    };
    std::vector<Cell> cells;

    // Apply で少しずつ追いかけている値（音ごと）
    struct Smoothed {
        Vector3 position = { 0.0f, 0.0f, 0.0f }; // 耳から見た位置（耳の位置を引いた物）
        float occlusion = 0.0f;
        bool isUsed = false; // この Update から Apply されたか（されなかった音は消す）
    };
    std::unordered_map<SoundHandle, Smoothed> smoothed;
};
