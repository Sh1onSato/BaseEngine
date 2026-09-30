#include "Calculation.h"
#include <cmath>
#include <numbers>
#include <algorithm>

// --- Matrix4x4 演算子オーバーロード ---

Matrix4x4 Matrix4x4::operator+(const Matrix4x4& other) const {
    Matrix4x4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = m[i][j] + other.m[i][j];
        }
    }
    return result;
}

Matrix4x4 Matrix4x4::operator-(const Matrix4x4& other) const {
    Matrix4x4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = m[i][j] - other.m[i][j];
        }
    }
    return result;
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& other) const {
    Matrix4x4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = 0;
            for (int k = 0; k < 4; ++k) {
                result.m[i][j] += m[i][k] * other.m[k][j];
            }
        }
    }
    return result;
}

// --- Calculation 静的関数 ---

float Calculation::Dot(const Vector3& a, const Vector3& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Calculation::Cross(const Vector3& a, const Vector3& b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x,
	};
}

float Calculation::Length(const Vector3& a) {
	return std::sqrt(Dot(a, a));
}

Vector3 Calculation::Normalize(const Vector3& a) {
	float length = Length(a);
	if (length == 0) {
		return { 0, 0, 0 };
	}
	return { a.x / length, a.y / length, a.z / length };
}

Matrix4x4 Calculation::Inverse(const Matrix4x4& a) {
	Matrix4x4 result;
	float det = 
		a.m[0][3] * a.m[1][2] * a.m[2][1] * a.m[3][0] - a.m[0][2] * a.m[1][3] * a.m[2][1] * a.m[3][0] -
		a.m[0][3] * a.m[1][1] * a.m[2][2] * a.m[3][0] + a.m[0][1] * a.m[1][3] * a.m[2][2] * a.m[3][0] +
		a.m[0][2] * a.m[1][1] * a.m[2][3] * a.m[3][0] - a.m[0][1] * a.m[1][2] * a.m[2][3] * a.m[3][0] -
		a.m[0][3] * a.m[1][2] * a.m[2][0] * a.m[3][1] + a.m[0][2] * a.m[1][3] * a.m[2][0] * a.m[3][1] +
		a.m[0][3] * a.m[1][0] * a.m[2][2] * a.m[3][1] - a.m[0][0] * a.m[1][3] * a.m[2][2] * a.m[3][1] -
		a.m[0][2] * a.m[1][0] * a.m[2][3] * a.m[3][1] + a.m[0][0] * a.m[1][2] * a.m[2][3] * a.m[3][1] +
		a.m[0][3] * a.m[1][1] * a.m[2][0] * a.m[3][2] - a.m[0][1] * a.m[1][3] * a.m[2][0] * a.m[3][2] -
		a.m[0][3] * a.m[1][0] * a.m[2][1] * a.m[3][2] + a.m[0][0] * a.m[1][3] * a.m[2][1] * a.m[3][2] +
		a.m[0][1] * a.m[1][0] * a.m[2][3] * a.m[3][2] - a.m[0][0] * a.m[1][1] * a.m[2][3] * a.m[3][2] -
		a.m[0][2] * a.m[1][1] * a.m[2][0] * a.m[3][3] + a.m[0][1] * a.m[1][2] * a.m[2][0] * a.m[3][3] +
		a.m[0][2] * a.m[1][0] * a.m[2][1] * a.m[3][3] - a.m[0][0] * a.m[1][2] * a.m[2][1] * a.m[3][3] -
		a.m[0][1] * a.m[1][0] * a.m[2][2] * a.m[3][3] + a.m[0][0] * a.m[1][1] * a.m[2][2] * a.m[3][3];

	if (det == 0.0f) {
		return {};
	}
	float invDet = 1.0f / det;

	result.m[0][0] = invDet * (
		a.m[1][2] * a.m[2][3] * a.m[3][1] - a.m[1][3] * a.m[2][2] * a.m[3][1] +
		a.m[1][3] * a.m[2][1] * a.m[3][2] - a.m[1][1] * a.m[2][3] * a.m[3][2] -
		a.m[1][2] * a.m[2][1] * a.m[3][3] + a.m[1][1] * a.m[2][2] * a.m[3][3]);

	result.m[0][1] = invDet * (
		a.m[0][3] * a.m[2][2] * a.m[3][1] - a.m[0][2] * a.m[2][3] * a.m[3][1] -
		a.m[0][3] * a.m[2][1] * a.m[3][2] + a.m[0][1] * a.m[2][3] * a.m[3][2] +
		a.m[0][2] * a.m[2][1] * a.m[3][3] - a.m[0][1] * a.m[2][2] * a.m[3][3]);

	result.m[0][2] = invDet * (
		a.m[0][2] * a.m[1][3] * a.m[3][1] - a.m[0][3] * a.m[1][2] * a.m[3][1] +
		a.m[0][3] * a.m[1][1] * a.m[3][2] - a.m[0][1] * a.m[1][3] * a.m[3][2] -
		a.m[0][2] * a.m[1][1] * a.m[3][3] + a.m[0][1] * a.m[1][2] * a.m[3][3]);

	result.m[0][3] = invDet * (
		a.m[0][3] * a.m[1][2] * a.m[2][1] - a.m[0][2] * a.m[1][3] * a.m[2][1] -
		a.m[0][3] * a.m[1][1] * a.m[2][2] + a.m[0][1] * a.m[1][3] * a.m[2][2] +
		a.m[0][2] * a.m[1][1] * a.m[2][3] - a.m[0][1] * a.m[1][2] * a.m[2][3]);

	result.m[1][0] = invDet * (
		a.m[1][3] * a.m[2][2] * a.m[3][0] - a.m[1][2] * a.m[2][3] * a.m[3][0] -
		a.m[1][3] * a.m[2][0] * a.m[3][2] + a.m[1][0] * a.m[2][3] * a.m[3][2] +
		a.m[1][2] * a.m[2][0] * a.m[3][3] - a.m[1][0] * a.m[2][2] * a.m[3][3]);

	result.m[1][1] = invDet * (
		a.m[0][2] * a.m[2][3] * a.m[3][0] - a.m[0][3] * a.m[2][2] * a.m[3][0] +
		a.m[0][3] * a.m[2][0] * a.m[3][2] - a.m[0][0] * a.m[2][3] * a.m[3][2] -
		a.m[0][2] * a.m[2][0] * a.m[3][3] + a.m[0][0] * a.m[2][2] * a.m[3][3]);

	result.m[1][2] = invDet * (
		a.m[0][3] * a.m[1][2] * a.m[3][0] - a.m[0][2] * a.m[1][3] * a.m[3][0] -
		a.m[0][3] * a.m[1][0] * a.m[3][2] + a.m[0][0] * a.m[1][3] * a.m[3][2] +
		a.m[0][2] * a.m[1][0] * a.m[3][3] - a.m[0][0] * a.m[1][2] * a.m[3][3]);

	result.m[1][3] = invDet * (
		a.m[0][2] * a.m[1][3] * a.m[2][0] - a.m[0][3] * a.m[1][2] * a.m[2][0] +
		a.m[0][3] * a.m[1][0] * a.m[2][2] - a.m[0][0] * a.m[1][3] * a.m[2][2] -
		a.m[0][2] * a.m[1][0] * a.m[2][3] + a.m[0][0] * a.m[1][2] * a.m[2][3]);

	result.m[2][0] = invDet * (
		a.m[1][1] * a.m[2][3] * a.m[3][0] - a.m[1][3] * a.m[2][1] * a.m[3][0] +
		a.m[1][3] * a.m[2][0] * a.m[3][1] - a.m[1][0] * a.m[2][3] * a.m[3][1] -
		a.m[1][1] * a.m[2][0] * a.m[3][3] + a.m[1][0] * a.m[2][1] * a.m[3][3]);

	result.m[2][1] = invDet * (
		a.m[0][3] * a.m[2][1] * a.m[3][0] - a.m[0][1] * a.m[2][3] * a.m[3][0] -
		a.m[0][3] * a.m[2][0] * a.m[3][1] + a.m[0][0] * a.m[2][3] * a.m[3][1] +
		a.m[0][1] * a.m[2][0] * a.m[3][3] - a.m[0][0] * a.m[2][1] * a.m[3][3]);

	result.m[2][2] = invDet * (
		a.m[0][1] * a.m[1][3] * a.m[3][0] - a.m[0][3] * a.m[1][1] * a.m[3][0] +
		a.m[0][3] * a.m[1][0] * a.m[3][1] - a.m[0][0] * a.m[1][3] * a.m[3][1] -
		a.m[0][1] * a.m[1][0] * a.m[3][3] + a.m[0][0] * a.m[1][1] * a.m[3][3]);

	result.m[2][3] = invDet * (
		a.m[0][3] * a.m[1][1] * a.m[2][0] - a.m[0][1] * a.m[1][3] * a.m[2][0] -
		a.m[0][3] * a.m[1][0] * a.m[2][1] + a.m[0][0] * a.m[1][3] * a.m[2][1] +
		a.m[0][1] * a.m[1][0] * a.m[2][3] - a.m[0][0] * a.m[1][1] * a.m[2][3]);

	result.m[3][0] = invDet * (
		a.m[1][2] * a.m[2][1] * a.m[3][0] - a.m[1][1] * a.m[2][2] * a.m[3][0] -
		a.m[1][2] * a.m[2][0] * a.m[3][1] + a.m[1][0] * a.m[2][2] * a.m[3][1] +
		a.m[1][1] * a.m[2][0] * a.m[3][2] - a.m[1][0] * a.m[2][1] * a.m[3][2]);

	result.m[3][1] = invDet * (
		a.m[0][1] * a.m[2][2] * a.m[3][0] - a.m[0][2] * a.m[2][1] * a.m[3][0] +
		a.m[0][2] * a.m[2][0] * a.m[3][1] - a.m[0][0] * a.m[2][2] * a.m[3][1] -
		a.m[0][1] * a.m[2][0] * a.m[3][2] + a.m[0][0] * a.m[2][1] * a.m[3][2]);

	result.m[3][2] = invDet * (
		a.m[0][2] * a.m[1][1] * a.m[3][0] - a.m[0][1] * a.m[1][2] * a.m[3][0] -
		a.m[0][2] * a.m[1][0] * a.m[3][1] + a.m[0][0] * a.m[1][2] * a.m[3][1] +
		a.m[0][1] * a.m[1][0] * a.m[3][2] - a.m[0][0] * a.m[1][1] * a.m[3][2]);

	result.m[3][3] = invDet * (
		a.m[0][1] * a.m[1][2] * a.m[2][0] - a.m[0][2] * a.m[1][1] * a.m[2][0] +
		a.m[0][2] * a.m[1][0] * a.m[2][1] - a.m[0][0] * a.m[1][2] * a.m[2][1] -
		a.m[0][1] * a.m[1][0] * a.m[2][2] + a.m[0][0] * a.m[1][1] * a.m[2][2]);

	return result;
}

Matrix4x4 Calculation::Transpose(const Matrix4x4& a) {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = a.m[j][i];
		}
	}
	return result;
}

Matrix4x4 Calculation::MakeIdentity4x4() {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}
	return result;
}

Matrix4x4 Calculation::MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	return result;
}

Matrix4x4 Calculation::MakeTranslationMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
}

Vector3 Calculation::Transform(const Vector3& vector, const Matrix4x4& matrix) {
	Vector3 result;
	result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + matrix.m[3][0];
	result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + matrix.m[3][1];
	result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + matrix.m[3][2];
	float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + matrix.m[3][3];

	assert(w != 0.0f);
	result.x /= w;
	result.y /= w;
	result.z /= w;

	return result;
}

Matrix4x4 Calculation::MakeRotationXMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	float cosR = std::cos(radian);
	float sinR = std::sin(radian);
	result.m[1][1] = cosR;
	result.m[1][2] = sinR;
	result.m[2][1] = -sinR;
	result.m[2][2] = cosR;
	return result;
}

Matrix4x4 Calculation::MakeRotationYMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	float cosR = std::cos(radian);
	float sinR = std::sin(radian);
	result.m[0][0] = cosR;
	result.m[0][2] = -sinR;
	result.m[2][0] = sinR;
	result.m[2][2] = cosR;
	return result;
}

Matrix4x4 Calculation::MakeRotationZMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	float cosR = std::cos(radian);
	float sinR = std::sin(radian);
	result.m[0][0] = cosR;
	result.m[0][1] = sinR;
	result.m[1][0] = -sinR;
	result.m[1][1] = cosR;
	return result;
}

Matrix4x4 Calculation::MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	return MakeScaleMatrix(scale) * 
           (MakeRotationXMatrix(rotate.x) * MakeRotationYMatrix(rotate.y) * MakeRotationZMatrix(rotate.z)) * 
           MakeTranslationMatrix(translate);
}

Matrix4x4 Calculation::MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate) {
	return MakeScaleMatrix(scale) * MakeRotateMatrix(rotate) * MakeTranslationMatrix(translate);
}

Matrix4x4 Calculation::MakeBillboardMatrix(const Vector3& scale, const Vector3& translate, const Matrix4x4& viewMatrix) {
	Matrix4x4 backToFrontMatrix = MakeIdentity4x4();
	// ビュー行列の逆行列（回転のみ）を取得
	backToFrontMatrix.m[0][0] = viewMatrix.m[0][0];
	backToFrontMatrix.m[0][1] = viewMatrix.m[1][0];
	backToFrontMatrix.m[0][2] = viewMatrix.m[2][0];
	backToFrontMatrix.m[1][0] = viewMatrix.m[0][1];
	backToFrontMatrix.m[1][1] = viewMatrix.m[1][1];
	backToFrontMatrix.m[1][2] = viewMatrix.m[2][1];
	backToFrontMatrix.m[2][0] = viewMatrix.m[0][2];
	backToFrontMatrix.m[2][1] = viewMatrix.m[1][2];
	backToFrontMatrix.m[2][2] = viewMatrix.m[2][2];

	return MakeScaleMatrix(scale) * backToFrontMatrix * MakeTranslationMatrix(translate);
}

Matrix4x4 Calculation::MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {  
    Matrix4x4 result = {};
    float tanHalfFovY = std::tan(fovY / 2.0f);  

    result.m[0][0] = 1.0f / (aspectRatio * tanHalfFovY);  
    result.m[1][1] = 1.0f / tanHalfFovY;  
    result.m[2][2] = farClip / (farClip - nearClip);  
    result.m[2][3] = 1.0f;  
    result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);  
    return result;  
}

Matrix4x4 Calculation::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 result = {};
	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;
	return result;
}

Matrix4x4 Calculation::MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = width / 2.0f;
	result.m[1][1] = -height / 2.0f;
	result.m[2][2] = maxDepth - minDepth;
	result.m[3][0] = left + (width / 2.0f);
	result.m[3][1] = top + (height / 2.0f);
	result.m[3][2] = minDepth;
	return result;
}

Matrix4x4 Calculation::MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up) {
	Vector3 forward = Normalize(target - eye);
	Vector3 right = Normalize(Cross(up, forward));
	Vector3 newUp = Cross(forward, right);

	Matrix4x4 result = {};
	result.m[0][0] = right.x;   result.m[0][1] = newUp.x;   result.m[0][2] = forward.x;   result.m[0][3] = 0.0f;
	result.m[1][0] = right.y;   result.m[1][1] = newUp.y;   result.m[1][2] = forward.y;   result.m[1][3] = 0.0f;
	result.m[2][0] = right.z;   result.m[2][1] = newUp.z;   result.m[2][2] = forward.z;   result.m[2][3] = 0.0f;
	result.m[3][0] = -Dot(right, eye);
	result.m[3][1] = -Dot(newUp, eye);
	result.m[3][2] = -Dot(forward, eye);
	result.m[3][3] = 1.0f;
	return result;
}

bool Calculation::TestRayAABB(const Ray& ray, const AABB& aabb, RaycastHit* outHit) {
	float tmin = 0.0f;
	float tmax = FLT_MAX;

	int hitAxis = -1;    // 入射した面の軸（0=X,1=Y,2=Z）。tmin を更新した軸を記録する
	float hitDir = 0.0f; // その軸のレイ進行方向の符号（法線の向き判定に使う）

	// X軸方向の判定
	if (std::abs(ray.direction.x) < 1e-6f) {
		if (ray.origin.x < aabb.min.x || ray.origin.x > aabb.max.x) return false;
	} else {
		float invD = 1.0f / ray.direction.x;
		float t1 = (aabb.min.x - ray.origin.x) * invD;
		float t2 = (aabb.max.x - ray.origin.x) * invD;
		if (t1 > t2) std::swap(t1, t2);
		if (t1 > tmin) { tmin = t1; hitAxis = 0; hitDir = ray.direction.x; }
		tmax = std::min(tmax, t2);
		if (tmin > tmax) return false;
	}

	// Y軸方向の判定
	if (std::abs(ray.direction.y) < 1e-6f) {
		if (ray.origin.y < aabb.min.y || ray.origin.y > aabb.max.y) return false;
	} else {
		float invD = 1.0f / ray.direction.y;
		float t1 = (aabb.min.y - ray.origin.y) * invD;
		float t2 = (aabb.max.y - ray.origin.y) * invD;
		if (t1 > t2) std::swap(t1, t2);
		if (t1 > tmin) { tmin = t1; hitAxis = 1; hitDir = ray.direction.y; }
		tmax = std::min(tmax, t2);
		if (tmin > tmax) return false;
	}

	// Z軸方向の判定
	if (std::abs(ray.direction.z) < 1e-6f) {
		if (ray.origin.z < aabb.min.z || ray.origin.z > aabb.max.z) return false;
	} else {
		float invD = 1.0f / ray.direction.z;
		float t1 = (aabb.min.z - ray.origin.z) * invD;
		float t2 = (aabb.max.z - ray.origin.z) * invD;
		if (t1 > t2) std::swap(t1, t2);
		if (t1 > tmin) { tmin = t1; hitAxis = 2; hitDir = ray.direction.z; }
		tmax = std::min(tmax, t2);
		if (tmin > tmax) return false;
	}

	// 衝突情報をセット
	if (outHit) {
		outHit->distance = tmin;
		outHit->hitPoint = ray.origin + ray.direction * tmin;

		// 入射した面の外向き法線を求める。
		// レイが正方向に進んでいれば min 側の面（法線は負）、負方向なら max 側の面（法線は正）に当たっている。
		Vector3 n = { 0.0f, 0.0f, 0.0f };
		if (hitAxis == 0)      n.x = (hitDir > 0.0f) ? -1.0f : 1.0f;
		else if (hitAxis == 1) n.y = (hitDir > 0.0f) ? -1.0f : 1.0f;
		else if (hitAxis == 2) n.z = (hitDir > 0.0f) ? -1.0f : 1.0f;
		else {
			// レイ始点が AABB 内部にある等で軸が確定しない場合は入射方向の逆向きで代用
			Vector3 rev = { -ray.direction.x, -ray.direction.y, -ray.direction.z };
			n = Normalize(rev);
		}
		outHit->normal = n;
	}

	return true;
}

bool Calculation::TestSphereAABB(const Sphere& sphere, const AABB& aabb, Vector3* outPushDir) {
	// AABB内で球の中心に最も近い点（各軸ごとにAABBの範囲内へクランプするだけで求まる）
	float closestX = (std::max)(aabb.min.x, (std::min)(sphere.center.x, aabb.max.x));
	float closestY = (std::max)(aabb.min.y, (std::min)(sphere.center.y, aabb.max.y));
	float closestZ = (std::max)(aabb.min.z, (std::min)(sphere.center.z, aabb.max.z));

	float dx = sphere.center.x - closestX;
	float dy = sphere.center.y - closestY;
	float dz = sphere.center.z - closestZ;
	float distanceSq = dx * dx + dy * dy + dz * dz;

	if (distanceSq > sphere.radius * sphere.radius) {
		return false;
	}

	if (outPushDir) {
		if (distanceSq > 0.0001f) {
			float dist = std::sqrt(distanceSq);
			*outPushDir = { dx / dist, dy / dist, dz / dist };
		} else {
			// 球の中心がAABBの面/辺/角の真上にある特異点。呼び出し側でゼロ扱いにして個別に対処すること
			*outPushDir = { 0.0f, 0.0f, 0.0f };
		}
	}
	return true;
}

AABB Calculation::MakeAABBFromTransform(const struct Transform& transform) {
	AABB aabb;
	aabb.min = { transform.translate.x - transform.scale.x, transform.translate.y - transform.scale.y, transform.translate.z - transform.scale.z };
	aabb.max = { transform.translate.x + transform.scale.x, transform.translate.y + transform.scale.y, transform.translate.z + transform.scale.z };
	return aabb;
}

Quaternion Calculation::Multiply(const Quaternion& q, const Quaternion& r) {
	return {
		q.w * r.x + q.x * r.w + q.y * r.z - q.z * r.y,
		q.w * r.y - q.x * r.z + q.y * r.w + q.z * r.x,
		q.w * r.z + q.x * r.y - q.y * r.x + q.z * r.w,
		q.w * r.w - q.x * r.x - q.y * r.y - q.z * r.z
	};
}

Quaternion Calculation::IdentityQuaternion() {
	return { 0.0f, 0.0f, 0.0f, 1.0f };
}

Quaternion Calculation::Conjugate(const Quaternion& q) {
	return { -q.x, -q.y, -q.z, q.w };
}

float Calculation::Norm(const Quaternion& q) {
	return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

Quaternion Calculation::Normalize(const Quaternion& q) {
	float norm = Norm(q);
	if (norm == 0.0f) return IdentityQuaternion();
	return { q.x / norm, q.y / norm, q.z / norm, q.w / norm };
}

Quaternion Calculation::Inverse(const Quaternion& q) {
	float norm = Norm(q);
	Quaternion conj = Conjugate(q);
	if (norm == 0.0f) return IdentityQuaternion();
	float normSq = norm * norm;
	return { conj.x / normSq, conj.y / normSq, conj.z / normSq, conj.w / normSq };
}

Quaternion Calculation::MakeAxisAngleQuaternion(const Vector3& axis, float angle) {
	Vector3 n = Normalize(axis);
	float sinA = std::sin(angle / 2.0f);
	return { n.x * sinA, n.y * sinA, n.z * sinA, std::cos(angle / 2.0f) };
}

Quaternion Calculation::DirectionToDirection(const Vector3& from, const Vector3& to) {
	Vector3 f = Normalize(from);
	Vector3 t = Normalize(to);
	float dot = Dot(f, t);

	// ほぼ同じ向き → 回転不要
	if (dot >= 1.0f - 1e-6f) {
		return IdentityQuaternion();
	}
	// ほぼ正反対（180度）→ 回転軸が定まらないので、fに垂直な任意軸で180度回す（特異点ガード）
	if (dot <= -1.0f + 1e-6f) {
		Vector3 axis = Cross({ 1.0f, 0.0f, 0.0f }, f);
		if (Length(axis) < 1e-6f) {
			axis = Cross({ 0.0f, 1.0f, 0.0f }, f);
		}
		return MakeAxisAngleQuaternion(Normalize(axis), kPi);
	}

	Vector3 axis = Cross(f, t);
	float angle = std::acos((std::max)(-1.0f, (std::min)(1.0f, dot)));
	return MakeAxisAngleQuaternion(axis, angle);
}

Matrix4x4 Calculation::MakeRotateMatrix(const Quaternion& q) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z;
	result.m[0][1] = 2.0f * (q.x * q.y + q.w * q.z);
	result.m[0][2] = 2.0f * (q.x * q.z - q.w * q.y);

	result.m[1][0] = 2.0f * (q.x * q.y - q.w * q.z);
	result.m[1][1] = q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z;
	result.m[1][2] = 2.0f * (q.y * q.z + q.w * q.x);

	result.m[2][0] = 2.0f * (q.x * q.z + q.w * q.y);
	result.m[2][1] = 2.0f * (q.y * q.z - q.w * q.x);
	result.m[2][2] = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z;
	return result;
}

Quaternion Calculation::Slerp(const Quaternion& q0, const Quaternion& q1, float t) {
	float dot = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
	Quaternion target = q1;
	if (dot < 0.0f) {
		target = { -q1.x, -q1.y, -q1.z, -q1.w };
		dot = -dot;
	}

	if (dot >= 1.0f - 0.0005f) {
		return {
			(1.0f - t) * q0.x + t * target.x,
			(1.0f - t) * q0.y + t * target.y,
			(1.0f - t) * q0.z + t * target.z,
			(1.0f - t) * q0.w + t * target.w
		};
	}

	float theta = std::acos(dot);
	float sinTheta = std::sin(theta);
	float scale0 = std::sin((1.0f - t) * theta) / sinTheta;
	float scale1 = std::sin(t * theta) / sinTheta;

	return {
		scale0 * q0.x + scale1 * target.x,
		scale0 * q0.y + scale1 * target.y,
		scale0 * q0.z + scale1 * target.z,
		scale0 * q0.w + scale1 * target.w
	};
}

void Calculation::DrawSphere(const Sphere& sphere, const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix, uint32_t color) {
    // 描画の実装は現状維持（あるいは描画クラスへ移動を推奨）
}

void Calculation::DrawGrid(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix) {
    // 描画の実装は現状維持
}
