#pragma once
#include <cstddef>

// スプライトのブレンドモード（SpriteCommonがモードごとにPSOを持つ）
enum class SpriteBlendMode {
	kNone,     // ブレンドなし（不透明上書き）
	kNormal,   // 通常のアルファブレンド
	kAdd,      // 加算
	kSubtract, // 減算
	kMultiply, // 乗算
	kScreen,   // スクリーン
	kCount,    // 個数（要素として使わない）
};

// ブレンドモードの総数
inline constexpr size_t kSpriteBlendModeCount = static_cast<size_t>(SpriteBlendMode::kCount);

// デバッグ表示用のブレンドモード名
inline const char* GetSpriteBlendModeName(SpriteBlendMode mode) {
	switch (mode) {
	case SpriteBlendMode::kNone: return "None";
	case SpriteBlendMode::kNormal: return "Normal";
	case SpriteBlendMode::kAdd: return "Add";
	case SpriteBlendMode::kSubtract: return "Subtract";
	case SpriteBlendMode::kMultiply: return "Multiply";
	case SpriteBlendMode::kScreen: return "Screen";
	default: return "Unknown";
	}
}
