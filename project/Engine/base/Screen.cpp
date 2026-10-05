#include "Screen.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

Screen::ScaleMode Screen::scaleMode = Screen::ScaleMode::kLetterbox;
uint32_t Screen::backBufferWidth = WinApp::KclientWidth;
uint32_t Screen::backBufferHeight = WinApp::KclientHeight;
Vector2 Screen::gameSize = { static_cast<float>(WinApp::KclientWidth), static_cast<float>(WinApp::KclientHeight) };
D3D12_VIEWPORT Screen::viewport = { 0.0f, 0.0f, static_cast<float>(WinApp::KclientWidth), static_cast<float>(WinApp::KclientHeight), 0.0f, 1.0f };
D3D12_RECT Screen::scissorRect = { 0, 0, WinApp::KclientWidth, WinApp::KclientHeight };

void Screen::SetScaleMode(ScaleMode mode) {
	scaleMode = mode;
	// 直近のバックバッファサイズで計算し直す（DirectXCommonは毎フレームPreDrawでビューポートを読み直す）
	UpdateLayout(backBufferWidth, backBufferHeight);
}

Vector2 Screen::GetBaseSize() {
	return { static_cast<float>(WinApp::KclientWidth), static_cast<float>(WinApp::KclientHeight) };
}

Vector2 Screen::GetSafeAreaOffset() {
	Vector2 baseSize = GetBaseSize();
	return { (gameSize.x - baseSize.x) * 0.5f, (gameSize.y - baseSize.y) * 0.5f };
}

Vector2 Screen::GetAnchoredPosition(const Vector2& anchor, const Vector2& offset) {
	return { gameSize.x * anchor.x + offset.x, gameSize.y * anchor.y + offset.y };
}

Vector2 Screen::ClientToGame(const Vector2& clientPosition) {
	// クライアント領域 = バックバッファなので、ビューポート内の相対位置をゲーム座標に拡大縮小するだけ
	if (viewport.Width <= 0.0f || viewport.Height <= 0.0f) {
		return clientPosition;
	}
	return {
		(clientPosition.x - viewport.TopLeftX) * gameSize.x / viewport.Width,
		(clientPosition.y - viewport.TopLeftY) * gameSize.y / viewport.Height,
	};
}

void Screen::UpdateLayout(uint32_t width, uint32_t height) {
	// 最小化中などで0が来たら前回のレイアウトを維持する
	if (width == 0 || height == 0) {
		return;
	}
	backBufferWidth = width;
	backBufferHeight = height;

	const float baseWidth = static_cast<float>(WinApp::KclientWidth);
	const float baseHeight = static_cast<float>(WinApp::KclientHeight);
	const float bufferWidth = static_cast<float>(width);
	const float bufferHeight = static_cast<float>(height);

	LONG left = 0;
	LONG top = 0;
	LONG right = static_cast<LONG>(width);
	LONG bottom = static_cast<LONG>(height);

	if (scaleMode == ScaleMode::kLetterbox) {
		// ベースのアスペクト比を保ったまま収まる最大の倍率で中央に置く
		gameSize = { baseWidth, baseHeight };
		const float scale = (std::min)(bufferWidth / baseWidth, bufferHeight / baseHeight);
		// 端数でにじまないよう整数ピクセルにそろえる（原点は切り捨て、サイズはバッファからはみ出さないように）
		LONG viewWidth = static_cast<LONG>(std::lround(baseWidth * scale));
		LONG viewHeight = static_cast<LONG>(std::lround(baseHeight * scale));
		viewWidth = std::clamp(viewWidth, 1L, static_cast<LONG>(width));
		viewHeight = std::clamp(viewHeight, 1L, static_cast<LONG>(height));
		left = (static_cast<LONG>(width) - viewWidth) / 2;
		top = (static_cast<LONG>(height) - viewHeight) / 2;
		right = left + viewWidth;
		bottom = top + viewHeight;
	} else {
		// バックバッファ全体に描画し、ベースより横長なら横に、縦長なら縦にゲーム座標を広げる
		if (bufferWidth / bufferHeight >= baseWidth / baseHeight) {
			gameSize = { baseHeight * bufferWidth / bufferHeight, baseHeight };
		} else {
			gameSize = { baseWidth, baseWidth * bufferHeight / bufferWidth };
		}
	}

	viewport.TopLeftX = static_cast<float>(left);
	viewport.TopLeftY = static_cast<float>(top);
	viewport.Width = static_cast<float>(right - left);
	viewport.Height = static_cast<float>(bottom - top);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	scissorRect.left = left;
	scissorRect.top = top;
	scissorRect.right = right;
	scissorRect.bottom = bottom;
}
