#pragma once
#include <string>
#include<d3d12.h>
#include<dxgi1_6.h>
#include<wrl.h> 
#include"externals/DirectXTex/DirectXTex.h"
#include"DirectXCommon.h"
#include"StringUtility.h"
#include"SrvManager.h"

using namespace StringUtility;

class TextureManager{
public:
	// 5x7ドットビットマップフォント 1文字分の定義（文字とビットパターンの対応）
	struct BitmapGlyph {
		char character;   // 対応する文字（アルファベットは大文字のみ保持。小文字はBitmapText側で大文字化してフォールバックする）
		uint8_t rows[7];  // 5x7ドットパターン（各行5bit、MSB側が左端の列、1=点灯）
	};

	// 数字(0-9)・記号(".", "-")・空白・アルファベット(A-Z)のビットパターンを1箇所にまとめた一覧テーブル。
	// CreateInternalDigitFontTexture()のテクスチャ生成と、BitmapText::GlyphIndex()の文字検索が
	// 共にこのテーブルを参照する（文字→ビットパターン対応の唯一の定義箇所）。
	// 既存文字（0-9, "." "-" 空白 W I N L O S E Q U T）は後方互換のため配列の並び順（=グリフインデックス）を変更しないこと。
	// 新しい文字を追加する場合は配列の末尾に追記すること。
	static constexpr BitmapGlyph kBitmapFontGlyphs[] = {
		{ '0', {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110} },
		{ '1', {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110} },
		{ '2', {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111} },
		{ '3', {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110} },
		{ '4', {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010} },
		{ '5', {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110} },
		{ '6', {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110} },
		{ '7', {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000} },
		{ '8', {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110} },
		{ '9', {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100} },
		{ '.', {0b00000,0b00000,0b00000,0b00000,0b00000,0b01100,0b01100} },
		{ '-', {0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000} },
		{ ' ', {0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000} },
		{ 'W', {0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001} },
		{ 'I', {0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110} },
		{ 'N', {0b10001,0b11001,0b10101,0b10101,0b10011,0b10001,0b10001} },
		{ 'L', {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111} },
		{ 'O', {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110} },
		{ 'S', {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110} },
		{ 'E', {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111} },
		{ 'Q', {0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101} },
		{ 'U', {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110} },
		{ 'T', {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100} },
		// ここから新規追加分（既存にないアルファベットを末尾に追加。既存グリフのインデックスは不変）
		{ 'A', {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001} },
		{ 'B', {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110} },
		{ 'C', {0b01111,0b10000,0b10000,0b10000,0b10000,0b10000,0b01111} },
		{ 'D', {0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110} },
		{ 'F', {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000} },
		{ 'G', {0b01111,0b10000,0b10000,0b10111,0b10001,0b10001,0b01111} },
		{ 'H', {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001} },
		{ 'J', {0b00001,0b00001,0b00001,0b00001,0b00001,0b10001,0b01110} },
		{ 'K', {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001} },
		{ 'M', {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001} },
		{ 'P', {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000} },
		{ 'R', {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001} },
		{ 'V', {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100} },
		{ 'X', {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001} },
		{ 'Y', {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100} },
		{ 'Z', {0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111} },
	};
	static constexpr int kBitmapFontGlyphCount = static_cast<int>(sizeof(kBitmapFontGlyphs) / sizeof(kBitmapFontGlyphs[0]));

	static TextureManager* GetInstance();
	// 初期化
	void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
	/// <summary>
	/// テクスチャファイルの読み込み
	/// </summary>
	/// <param name="filePath">テクスチャファイルのパス</param>
	void LoadTexture(const std::string& filePath);

	/// <summary>
	/// 1x1の白いテクスチャを内部で生成する
	/// </summary>
	void CreateInternalWhiteTexture();

	/// <summary>
	/// 数字("0"-"9")・"."・"-"・空白・アルファベット(A-Z)のグリフアトラス（"digitFont"）を内部で生成する。
	/// ビットパターンの定義はkBitmapFontGlyphsを参照。既に生成済みなら何もしない
	/// </summary>
	void CreateInternalDigitFontTexture();

	uint32_t GetTextureIndexByFilePath(const std::string& filePath);
	uint32_t GetSrvIndex(uint32_t textureIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(uint32_t textureIndex);

	void Finalize();

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);
	const DirectX::TexMetadata& GetMetadata(uint32_t textureIndex);
	
private:
	DirectXCommon* dxCommon = nullptr;
	SrvManager* srvManager = nullptr;

	static TextureManager* instance;
	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(TextureManager&) = delete;
	TextureManager& operator = (TextureManager&) = delete;

	struct TextureData {
		std::string filePath;
		DirectX::TexMetadata metadata;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint32_t srvIndex;
	};
	std::vector<TextureData> textureDatas;


	
};

