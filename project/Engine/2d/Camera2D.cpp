#include "Camera2D.h"
#include "Calculation.h"
#include "Screen.h"
#include <cmath>

Matrix4x4 Camera2D::GetViewMatrix() const {
	// 画面中央（ゲーム座標）
	Vector2 center = Screen::GetGameSize() * 0.5f;
	// カメラ位置を原点へ → カメラの逆回転 → 拡大 → 画面中央へ
	return Calculation::MakeTranslationMatrix({ -position.x, -position.y, 0.0f }) *
		Calculation::MakeRotationZMatrix(-rotation) *
		Calculation::MakeScaleMatrix({ zoom, zoom, 1.0f }) *
		Calculation::MakeTranslationMatrix({ center.x, center.y, 0.0f });
}

Vector2 Camera2D::WorldToScreen(const Vector2& world) const {
	Vector2 center = Screen::GetGameSize() * 0.5f;
	Vector2 d = world - position;
	// -rotation 回転（MakeRotationZMatrixの行ベクトル規約に合わせる）
	float c = std::cos(rotation);
	float s = std::sin(rotation);
	Vector2 rotated = { d.x * c + d.y * s, -d.x * s + d.y * c };
	return rotated * zoom + center;
}

Vector2 Camera2D::ScreenToWorld(const Vector2& screen) const {
	// ズーム0で割らないようにする
	if (zoom == 0.0f) {
		return position;
	}
	Vector2 center = Screen::GetGameSize() * 0.5f;
	Vector2 d = (screen - center) * (1.0f / zoom);
	// +rotation 回転して元の向きに戻す
	float c = std::cos(rotation);
	float s = std::sin(rotation);
	Vector2 rotated = { d.x * c - d.y * s, d.x * s + d.y * c };
	return rotated + position;
}
