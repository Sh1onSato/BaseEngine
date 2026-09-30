#pragma once
#include "Structs.h"
#include <cmath>
#include <assert.h>
#include <stdint.h>
#include <iostream>
#include <numbers>

class Calculation {
public:
	// インスタンス化を禁止
	Calculation() = delete;

	// 円周率（各所で桁数の違う値がバラバラに直書きされていたのをここに集約）
	static constexpr float kPi = std::numbers::pi_v<float>;

	// 固定フレームレート前提の暫定デルタタイム（本来は実フレーム時間を受け取るのが望ましいが、
	// 現状複数箇所が「暫定60FPS」を個別にハードコードしていたためここに集約）
	static constexpr float kFixedDeltaTime = 1.0f / 60.0f;

	// ベクトル演算
	static Vector3 Add(const Vector3& a, const Vector3& b) { return a + b; }
	static Vector3 Subtract(const Vector3& a, const Vector3& b) { return a - b; }
	static Vector3 Multiply(float b, const Vector3& a) { return a * b; }
	static float Dot(const Vector3& a, const Vector3& b);
	static Vector3 Cross(const Vector3& a, const Vector3& b);
	static float Length(const Vector3& a);
	static Vector3 Normalize(const Vector3& a);

	// 行列演算
	static Matrix4x4 Add(const Matrix4x4& a, const Matrix4x4& b) { return a + b; }
	static Matrix4x4 Subtract(const Matrix4x4& a, const Matrix4x4& b) { return a - b; }
	static Matrix4x4 MatrixMultiply(const Matrix4x4& a, const Matrix4x4& b) { return a * b; }
	static Matrix4x4 Inverse(const Matrix4x4& a);
	static Matrix4x4 Transpose(const Matrix4x4& a);
	static Matrix4x4 MakeIdentity4x4();

	static Matrix4x4 MakeScaleMatrix(const Vector3& scale);
	static Matrix4x4 MakeTranslationMatrix(const Vector3& translate);
	static Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);

	static Matrix4x4 MakeRotationXMatrix(float radian);
	static Matrix4x4 MakeRotationYMatrix(float radian);
	static Matrix4x4 MakeRotationZMatrix(float radian);

	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);
	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);

	static Matrix4x4 MakeBillboardMatrix(const Vector3& scale, const Vector3& translate, const Matrix4x4& viewMatrix);

	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farCrip);
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
	static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);
	// eyeからtargetを見るビュー行列を作成（行ベクトル規約。Inverse(MakeAffineMatrix(...))と等価）
	static Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up);

	// 当たり判定
	static bool TestRayAABB(const Ray& ray, const AABB& aabb, RaycastHit* outHit = nullptr);
	// 球 vs AABB（最近接点法）。outPushDirにはAABB内の最近接点から球の中心へ向かう正規化方向（めり込み解消・スライド移動に使う押し出し方向）を返す
	static bool TestSphereAABB(const Sphere& sphere, const AABB& aabb, Vector3* outPushDir = nullptr);
	// Transformからaxis-aligned AABBを作る（translateを中心、scaleを各軸の半辺長とみなす。箱型のObject3dのTransformをそのまま渡せる）
	// ※クラス内に同名の関数Transform()が既にあるため、型のTransformと区別するためstructを明示している
	static AABB MakeAABBFromTransform(const struct Transform& transform);

	// クォータニオン演算
	static Quaternion Multiply(const Quaternion& q, const Quaternion& r);
	static Quaternion IdentityQuaternion();
	static Quaternion Conjugate(const Quaternion& q);
	static float Norm(const Quaternion& q);
	static Quaternion Normalize(const Quaternion& q);
	static Quaternion Inverse(const Quaternion& q);
	static Quaternion MakeAxisAngleQuaternion(const Vector3& axis, float angle);
	// from方向をto方向へ最短で向ける回転クォータニオン（±Z等の正反対も特異点ガード済み）
	static Quaternion DirectionToDirection(const Vector3& from, const Vector3& to);
	static Matrix4x4 MakeRotateMatrix(const Quaternion& q);
	static Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);

	// デバッグ用描画（実際の実装は描画クラスで行うべきだが、現状維持）
	static void DrawSphere(const Sphere& sphere, const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix, uint32_t color);
	static void DrawGrid(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix);
};
