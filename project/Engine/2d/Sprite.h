#pragma once
#include"Structs.h"
#include"SpriteBlendMode.h"
#include "WinApp.h"
#include <d3d12.h>
#include <wrl.h>
class SpriteCommon;
class DirectXCommon;

class Sprite {
public:
	// テクスチャのサンプリング方法
	enum class TextureFilter {
		kLinear, // リニア補間（なめらか）
		kPoint,  // ポイント（ドット絵向け、くっきり）
	};

	// 初期化
	void Initialize(SpriteCommon* spriteCommon,std::string textureFilePath);
	//更新（頂点・UV・ワールド行列を計算する。WVPは描画時に RecordDrawCommand で計算される）
	void Update();
	//描画
	void Draw();
	void Draw(uint32_t textureIndex); 
	void DrawSRV(uint32_t srvIndex); // 追加：SRVインデックスを直接指定して描画
	// ※ Draw系はSpriteCommonの描画キューに積むだけ。実際の描画はSpriteCommon::Flushでレイヤー順に行われる

	// 実際に描画コマンドを積む。SpriteCommon::Flush から呼ばれる。ゲーム側は Draw() を使う
	// （ここでカメラ込みのWVPを計算して定数バッファに書き込む）
	void RecordDrawCommand(uint32_t srvIndex);

	void ImGui(); // デバッグ用UIを表示する関数を追加
	// --- 外部から数値を取得するための関数 (Getter) ---
	const Vector2& GetAnchorPoint() const { return anchorPoint; }
	const bool& GetIsFlipX() const { return isFlipX; }
	const bool& GetIsFlipY() const { return isFlipY; }
	const Vector2& GetTexLeftTop() const { return texLeftTop; }
	const Vector2& GetTexSize() const { return texSize; }
	SpriteBlendMode GetBlendMode() const { return blendMode; }
	int GetLayer() const { return layer; }
	bool GetUseCamera() const { return useCamera; }
	TextureFilter GetTextureFilter() const {
		return materialData->usePointFilter != 0 ? TextureFilter::kPoint : TextureFilter::kLinear;
	}

	// --- 外部から数値をいじるための関数 (Setter) ---
	void SetPosition(const Vector2& pos) { transform.translate = { pos.x, pos.y, 0.0f }; }
	void SetSize(const Vector2& size) { this->size = size; }
	void SetRotation(float rotation) { transform.rotate.z = rotation; }
	void SetAnchorPoint(const Vector2& anchorPoint) { this->anchorPoint = anchorPoint; }
	void SetIsFlipX(bool isFlipX) { this->isFlipX = isFlipX; }
	void SetIsFlipY(bool isFlipY) { this->isFlipY = isFlipY; }
	void SetTexLeftTop(const Vector2& texLeftTop) { this->texLeftTop = texLeftTop; }
	void SetTexSize(const Vector2& texSize) { this->texSize = texSize; }
	void SetColor(const Vector4& color) { materialData->color = color; }
	// 描画時に使うブレンドモード（RecordDrawCommand内でSpriteCommonに反映される）
	void SetBlendMode(SpriteBlendMode mode) { blendMode = mode; }
	// 描画レイヤー（大きいほど手前。同じレイヤー内はDrawを呼んだ順）
	void SetLayer(int layer) { this->layer = layer; }
	// trueでワールド座標として扱い、SpriteCommonにセットされたCamera2Dの影響を受ける
	// （既定はfalse＝UI用の画面座標。カメラ未設定ならtrueでも画面座標のまま）
	void SetUseCamera(bool useCamera) { this->useCamera = useCamera; }
	// テクスチャのサンプリング方法を切り替える
	void SetTextureFilter(TextureFilter filter) {
		materialData->usePointFilter = (filter == TextureFilter::kPoint) ? 1 : 0;
	}
private:
	SpriteCommon* spriteCommon = nullptr;
	DirectXCommon* dxCommon = nullptr;

	Microsoft::WRL::ComPtr <ID3D12Resource> vertexResourceSprite;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite;
	SpriteMaterial* materialData = nullptr;
	Transform uvTransformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	SpriteVertex* VertexDataSprite = nullptr;
	// 描画時のブレンドモード
	SpriteBlendMode blendMode = SpriteBlendMode::kNormal;
	// 描画レイヤー（大きいほど手前）
	int layer = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite;
	//Sprite用のTransformationMatrix用のリソースを作成
	Microsoft::WRL::ComPtr <ID3D12Resource> transformationMatrixResourceSprite;
	TransformationMatrix* transformetionMatrixDataSprite;
	// スプライト自身の座標・回転・サイズ
	Transform transform{ { 1.0f, 1.0f, 1.0f } , { 0.0f, 0.0f, 0.0f } , { 0.0f, 0.0f, 0.0f } };
	// Updateで計算したワールド行列（WVPは描画時にカメラと合成する）
	Matrix4x4 worldMatrix{};
	// Camera2Dの影響を受けるか（false＝UI用の画面座標）
	bool useCamera = false;
	// ワールド行列・カメラ・正射影からWVPを計算して定数バッファに書き込む
	void UpdateTransformationMatrix();

	Microsoft::WRL::ComPtr < ID3D12Resource> indexResourceSprite;
	D3D12_INDEX_BUFFER_VIEW indexBuffViewSprite{};
	uint32_t* indexDataSprite = nullptr;

	// --- 数値を直接打たずに変数で管理する ---
	Vector2 size = { 640.0f, 360.0f };
	// テクスチャ番号
	uint32_t textureIndex = 0;
	// デバッグ用にファイル名を保存しておく変数
	std::string name;

	Vector2 anchorPoint = { 0.0f, 0.0f }; 

	// 左右フリップ
	bool isFlipX = false;
	// 上下フリップ
	bool isFlipY = false;

	// テクスチャ左上座標
	Vector2 texLeftTop = { 0.0f, 0.0f };
	// テクスチャの切り出し
	Vector2 texSize = { 100.0f, 100.0f };
	// テクスチャサイズをイメージに合わせる
	void AdjustTextureSize();
};

