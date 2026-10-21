#pragma once
#include "Structs.h"
#include <cstdint>
#include <string>
#include <unordered_map>

class Sprite;

// スプライトシート（1枚の画像にコマを並べたもの）のパラパラアニメーション
// 【使い方】
//   animation.SetSheet(4, 2, { 64.0f, 64.0f });            // 4列x2行、1コマ64x64ピクセル
//   animation.AddClip("idle", 0, 4, 6.0f, true);          // 0〜3コマを6fpsでループ
//   animation.Play("idle");
//   毎フレーム： animation.Update(Time::GetDeltaTime()); animation.Apply(sprite); sprite.Update();
// ・コマ番号は左上から右へ、行の終わりで次の行へ（左→右、上→下）
// ・frameSize はテクスチャのピクセル単位で必ず指定する（テクスチャサイズからの自動計算はしない）
// ・Apply は切り出し範囲（texLeftTop / texSize）だけを変える。表示サイズ（SetSize）は変えない
class SpriteAnimation {
public:
	// シートのレイアウトを設定する（columns列 x rows行、1コマの大きさ frameSize[px]）
	void SetSheet(uint32_t columns, uint32_t rows, const Vector2& frameSize);

	// クリップを追加する（同じ名前なら上書き）
	// startFrame から frameCount コマを framesPerSecond の速さで再生する
	void AddClip(const std::string& name, uint32_t startFrame, uint32_t frameCount, float framesPerSecond, bool loop);

	// クリップを再生する。同じクリップを再生中で restart=false なら何もしない
	// 登録されていない名前は警告ログを出して無視する
	void Play(const std::string& name, bool restart = false);

	// 時間を進める（deltaTime: 秒）
	void Update(float deltaTime);

	// 現在のコマの切り出し範囲をSpriteに設定する
	void Apply(Sprite& sprite) const;

	// ループしないクリップが最後のコマを1コマ分表示し終わったか（その後も最後のコマのまま止まる）
	bool IsFinished() const { return isFinished; }
	// 現在のコマ番号（シート全体での番号）
	uint32_t GetCurrentFrame() const;
	// 再生中のクリップ名（未再生なら空文字）
	const std::string& GetCurrentClipName() const { return currentClipName; }
	// 再生速度の倍率（1.0で等速）
	void SetSpeed(float speed) { this->speed = speed; }
	float GetSpeed() const { return speed; }

private:
	// アニメーションの1区間
	struct Clip {
		uint32_t startFrame = 0;       // 開始コマ
		uint32_t frameCount = 1;       // コマ数
		float framesPerSecond = 10.0f; // 1秒あたりのコマ数
		bool loop = true;              // ループするか
	};

	// シートのレイアウト
	uint32_t columns = 1;
	uint32_t rows = 1;
	Vector2 frameSize = { 0.0f, 0.0f };

	// 登録済みクリップ
	std::unordered_map<std::string, Clip> clips;
	// 再生中のクリップ（clips内を指す。未再生ならnullptr）
	const Clip* currentClip = nullptr;
	std::string currentClipName;
	// クリップ内のコマ位置（0〜frameCount-1）
	uint32_t clipFrameIndex = 0;
	// 次のコマに進むまでの経過時間
	float elapsed = 0.0f;
	// 再生速度の倍率
	float speed = 1.0f;
	// ループしないクリップが終わったか
	bool isFinished = false;
};
