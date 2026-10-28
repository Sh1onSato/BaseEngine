#pragma once
#include <string>
#include <vector>
#include <stdint.h>

// --- 基本構造体 ---

struct Vector2 {
	float x, y;

	Vector2 operator+(const Vector2& other) const { return { x + other.x, y + other.y }; }
	Vector2 operator-(const Vector2& other) const { return { x - other.x, y - other.y }; }
	Vector2 operator*(float scalar) const { return { x * scalar, y * scalar }; }
};

struct Vector3 {
	float x, y, z;

	Vector3 operator+(const Vector3& other) const { return { x + other.x, y + other.y, z + other.z }; }
	Vector3 operator-(const Vector3& other) const { return { x - other.x, y - other.y, z - other.z }; }
	Vector3 operator*(float scalar) const { return { x * scalar, y * scalar, z * scalar }; }
	Vector3& operator+=(const Vector3& other) { x += other.x; y += other.y; z += other.z; return *this; }
	Vector3& operator-=(const Vector3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
	Vector3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
};

struct Vector4 {
	float x, y, z, w;
};

struct Matrix3x3 {
	float m[3][3];
};

struct Matrix4x4 {
	float m[4][4];

	Matrix4x4 operator+(const Matrix4x4& other) const;
	Matrix4x4 operator-(const Matrix4x4& other) const;
	Matrix4x4 operator*(const Matrix4x4& other) const;
};

struct Quaternion {
	float x, y, z, w;
};

// --- 形状・衝突用構造体 ---

struct Sphere {
	Vector3 center;
	float radius;
};

struct Ray {
	Vector3 origin;
	Vector3 direction;
};

struct AABB {
	Vector3 min;
	Vector3 max;
};

struct RaycastHit {
	Vector3 hitPoint; // 衝突地点
	float distance;   // 始点からの距離
	Vector3 normal;   // 法線
};

// --- エンジン用構造体 ---

struct Transform {
	Vector3 scale,
		rotate,
		translate;
};

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
	// --- スキニング用（フェーズ1で追加。デフォルト値により既存の初期化箇所は無修正で動作する） ---
	Vector4 boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
	uint32_t boneIndices[4] = { 0, 0, 0, 0 };
};

// 物ごとの材質（Object3d・ModelInstancer が持つ。Object3d.PS.hlsl の Material と同じ並び）
struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
	// 自分で光る色（rgb に強さを掛けた物。0 なら光らない）。モデルの発光に足される
	Vector4 emissive = { 0.0f, 0.0f, 0.0f, 0.0f };
	// 0 以上ならモデルの値の代わりに使う（-1 = モデルのまま）
	float metallicOverride = -1.0f;
	float roughnessOverride = -1.0f;
	// 光の当たり方: -1 = モデルのまま、0 = 今までの簡単な当たり方、1 = 金属・粗さを使う当たり方
	int32_t lightingModelOverride = -1;
	float padding2 = 0.0f;
};
static_assert(sizeof(Material) == 128);

// モデルの部分ごとの材質（Model が持つ。Object3d.PS.hlsl の SurfaceMaterial と同じ並び）
struct SurfaceMaterial {
	// 自分で光る色（rgb に強さを掛けた物）
	Vector4 emissive = { 0.0f, 0.0f, 0.0f, 0.0f };
	float metallic = 0.0f;           // 0 = 金属でない、1 = 金属
	float roughness = 0.5f;          // 0 = つるつる（光が小さく強く映る）、1 = ざらざら
	float normalStrength = 1.0f;     // 法線マップの強さ
	float occlusionStrength = 1.0f;  // 影になりやすい所の画像の強さ
	// どの画像を使うか（SurfaceMaterialFlags を足した物）
	uint32_t flags = 0;
	float padding[3] = {};
};
static_assert(sizeof(SurfaceMaterial) == 48);

// SurfaceMaterial::flags
namespace SurfaceMaterialFlags {
	constexpr uint32_t kNormalMap = 1u << 0;
	constexpr uint32_t kMetallicRoughnessMap = 1u << 1; // glTF と同じ: 緑 = 粗さ、青 = 金属
	constexpr uint32_t kEmissiveMap = 1u << 2;
	constexpr uint32_t kOcclusionMap = 1u << 3;         // 赤を使う
	constexpr uint32_t kPhysicalLighting = 1u << 4;     // 金属・粗さを使う当たり方にする
}

// カメラの位置（光の照り返しの計算に使う。Object3d.PS.hlsl の Camera と同じ並び）
struct CameraForGPU {
	Vector3 worldPosition;
	float padding;
};

struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

// --- スプライト専用（Sprite.VS.hlsl / Sprite.PS.hlsl と対応） ---
struct SpriteVertex {
	Vector4 position;
	Vector2 texcoord;
};

struct SpriteMaterial {
	Vector4 color;
	Matrix4x4 uvTransform;
	int32_t usePointFilter; // 0: リニア補間 / 1: ポイント(ニアレスト)
	float padding[3];
};
static_assert(sizeof(SpriteMaterial) == 96);

struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
};

struct PointLight {
	Vector4 color;
	Vector3 position;
	float intensity;
	float radius;
	float decay;
	float padding[2];
};
static_assert(sizeof(PointLight) == 48);

struct SpotLight {
	Vector4 color;
	Vector3 position;
	float intensity;
	Vector3 direction;
	float distance;
	float decay;
	float cosAngle;
	float cosFalloffStart;
	float padding;
};
static_assert(sizeof(SpotLight) == 64);

struct ShadowData {
	Matrix4x4 lightViewProjection;
	float bias;
	float orthoExtentWorld;     // 正射影の片側半幅（PS側のPCSS計算でワールド距離⇔UV距離の換算に使用）
	float orthoDepthRangeWorld; // ファークリップ - ニアクリップ
	float padding;
};
static_assert(sizeof(ShadowData) == 80);

struct MaterialData {
	std::string textureFilePath;
	// .mtl の追加の項目（無ければ空・-1）
	std::string normalMapFilePath;   // map_Bump / bump / norm
	float normalStrength = 1.0f;     // map_Bump の -bm
	std::string emissiveMapFilePath; // map_Ke
	Vector3 emissive = { 0.0f, 0.0f, 0.0f }; // Ke
	float metallic = -1.0f;          // Pm（あれば金属・粗さを使う当たり方にする）
	float roughness = -1.0f;         // Pr
};

struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};
