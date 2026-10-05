#pragma once
#include <d3d12.h>
#include <cstdint>
#include "Structs.h"

// 画面サイズとゲーム座標の管理（DirectXCommonがバックバッファ作成・リサイズ時にUpdateLayout()する）。
// バックバッファは常にウィンドウのクライアント領域と同じサイズになり、
// ゲームの絵はその中のビューポート（GetViewport()）に描画される。
// ゲーム側の座標（Spriteの正射影・マウス座標・UI）は「ゲーム座標」(GetGameSize())で扱うこと。
class Screen {
public:
	Screen() = delete;

	// 画面サイズがベース解像度と違うときの合わせ方
	enum class ScaleMode {
		kLetterbox, // ベースのアスペクト比を保って中央に表示し、余白を黒帯にする（ゲーム座標は常にベースサイズ）
		kExpand,    // バックバッファ全体に描画し、余った方向にゲーム座標を広げる
	};

	// モードを切り替えて、直近のバックバッファサイズでレイアウトを計算し直す
	static void SetScaleMode(ScaleMode mode);
	static ScaleMode GetScaleMode() { return scaleMode; }

	// ベース解像度（WinApp::KclientWidth x KclientHeight）
	static Vector2 GetBaseSize();
	// ゲーム座標のサイズ（Spriteの正射影やUI配置の基準）
	static Vector2 GetGameSize() { return gameSize; }
	// ゲーム座標のアスペクト比（カメラの射影行列用）
	static float GetAspectRatio() { return gameSize.x / gameSize.y; }
	// ベースサイズの領域がゲーム座標のどこにあるか（kExpandで広がった分の半分。kLetterboxでは0）
	static Vector2 GetSafeAreaOffset();
	// 画面の基準点(anchor: 0〜1。{0,0}=左上、{1,1}=右下)からoffsetだけずらしたゲーム座標
	static Vector2 GetAnchoredPosition(const Vector2& anchor, const Vector2& offset);
	// ウィンドウのクライアント座標（ピクセル）をゲーム座標に変換する
	static Vector2 ClientToGame(const Vector2& clientPosition);

	// バックバッファ内でゲームの絵を描く範囲（ピクセル）
	static const D3D12_VIEWPORT& GetViewport() { return viewport; }
	static const D3D12_RECT& GetScissorRect() { return scissorRect; }

	static uint32_t GetBackBufferWidth() { return backBufferWidth; }
	static uint32_t GetBackBufferHeight() { return backBufferHeight; }

	// DirectXCommon専用。バックバッファのサイズからビューポートとゲーム座標サイズを計算する
	static void UpdateLayout(uint32_t width, uint32_t height);

private:
	static ScaleMode scaleMode;
	static uint32_t backBufferWidth;
	static uint32_t backBufferHeight;
	static Vector2 gameSize;
	static D3D12_VIEWPORT viewport;
	static D3D12_RECT scissorRect;
};
