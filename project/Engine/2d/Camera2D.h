#pragma once
#include "Structs.h"

// 2Dカメラ（ワールド座標 → ゲーム座標(画面)への変換）
// ・position : 画面中央に映すワールド座標
// ・zoom     : 拡大率（2.0で2倍の大きさに見える）
// ・rotation : カメラの回転（ラジアン）
// 使い方：SpriteCommon::SetCamera2D でセットし、Sprite::SetUseCamera(true) にしたSpriteだけがカメラの影響を受ける。
// ※ 画面中央は Screen::GetGameSize() の半分なので、ウィンドウサイズやScaleModeが変わっても中央に映る。
class Camera2D {
public:
	// --- Setter ---
	void SetPosition(const Vector2& position) { this->position = position; }
	void SetZoom(float zoom) { this->zoom = zoom; }
	void SetRotation(float rotation) { this->rotation = rotation; }
	// 現在の位置からdeltaだけ動かす（ワールド座標）
	void Move(const Vector2& delta) { position = position + delta; }

	// --- Getter ---
	const Vector2& GetPosition() const { return position; }
	float GetZoom() const { return zoom; }
	float GetRotation() const { return rotation; }

	// ワールド座標 → ゲーム座標 のビュー行列（行ベクトル規約：world * view * proj）
	Matrix4x4 GetViewMatrix() const;

	// ワールド座標 → ゲーム座標（Input::GetMousePosition()と同じ空間）
	Vector2 WorldToScreen(const Vector2& world) const;
	// ゲーム座標 → ワールド座標（WorldToScreenの逆変換）
	Vector2 ScreenToWorld(const Vector2& screen) const;

private:
	// 画面中央に映すワールド座標
	Vector2 position = { 0.0f, 0.0f };
	// 拡大率
	float zoom = 1.0f;
	// 回転（ラジアン）
	float rotation = 0.0f;
};
