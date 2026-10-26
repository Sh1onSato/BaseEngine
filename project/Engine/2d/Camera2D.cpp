#include "Camera2D.h"
#include "Calculation.h"
#include "Screen.h"
#include <cmath>

namespace {
	// 揺れの幅 1 ピクセルあたりの傾き（ラジアン）
	constexpr float kShakeRollPerPixel = 0.0015f;
}

Vector2 Camera2D::GetShakenPosition() const {
	// 揺れ幅は画面のピクセルなので、ワールドの長さに直す（ズームしていても同じ幅に見える）
	const Vector3 offset = shake.GetOffset();
	const float scale = zoom != 0.0f ? 1.0f / zoom : 1.0f;
	return { position.x + offset.x * scale, position.y + offset.y * scale };
}

float Camera2D::GetShakenRotation() const {
	return rotation + shake.GetOffset().z * kShakeRollPerPixel;
}

Matrix4x4 Camera2D::GetViewMatrix() const {
	// 画面中央（ゲーム座標）
	Vector2 center = Screen::GetGameSize() * 0.5f;
	const Vector2 shakenPosition = GetShakenPosition();
	// カメラ位置を原点へ → カメラの逆回転 → 拡大 → 画面中央へ
	return Calculation::MakeTranslationMatrix({ -shakenPosition.x, -shakenPosition.y, 0.0f }) *
		Calculation::MakeRotationZMatrix(-GetShakenRotation()) *
		Calculation::MakeScaleMatrix({ zoom, zoom, 1.0f }) *
		Calculation::MakeTranslationMatrix({ center.x, center.y, 0.0f });
}

Vector2 Camera2D::WorldToScreen(const Vector2& world) const {
	Vector2 center = Screen::GetGameSize() * 0.5f;
	Vector2 d = world - GetShakenPosition();
	// -rotation 回転（MakeRotationZMatrixの行ベクトル規約に合わせる）
	const float shakenRotation = GetShakenRotation();
	float c = std::cos(shakenRotation);
	float s = std::sin(shakenRotation);
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
	const float shakenRotation = GetShakenRotation();
	float c = std::cos(shakenRotation);
	float s = std::sin(shakenRotation);
	Vector2 rotated = { d.x * c - d.y * s, d.x * s + d.y * c };
	return rotated + GetShakenPosition();
}
