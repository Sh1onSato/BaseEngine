#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include<d3d12.h>
#include<dxgi1_6.h>
#include<wrl.h> 
#include"externals/DirectXTex/DirectXTex.h"
#include"DirectXCommon.h"
#include"StringUtility.h"
#include"SrvManager.h"

using namespace StringUtility;

/// <summary>
/// TextureManager の画像を「使っている」印（参照カウント）。持っている間はその画像が解放されない。
/// コピーすると数が増え、破棄・Reset すると減る。Sprite・Model・Skybox が中で持っているので、普段は意識しなくてよい。
/// ゲーム側でシーンをまたいで残したい画像があれば、TextureManager::Acquire で受け取って持っておく（MarkPersistent でもよい）。
/// </summary>
class TextureHandle {
public:
	TextureHandle() = default;
	~TextureHandle() { Reset(); }
	TextureHandle(const TextureHandle& other);
	TextureHandle& operator=(const TextureHandle& other);
	TextureHandle(TextureHandle&& other) noexcept;
	TextureHandle& operator=(TextureHandle&& other) noexcept;

	bool IsValid() const { return index != kInvalidIndex; }
	// テクスチャ番号（TextureManager の GetSrvHandleGPU などに渡す）
	uint32_t GetIndex() const { return index; }
	// 使うのをやめる
	void Reset();

private:
	friend class TextureManager;
	static constexpr uint32_t kInvalidIndex = UINT32_MAX;
	// TextureManager が数を増やしてから渡す
	explicit TextureHandle(uint32_t textureIndex) : index(textureIndex) {}

	uint32_t index = kInvalidIndex;
};

/// テクスチャの読み込みと解放。
/// 解放のしかた（参照カウント）:
///   ・使っている物（TextureHandle。Sprite・Model・Skybox が持つ）が 0 になった画像は、すぐには消さない
///   ・シーンが切り替わると、前のシーンまでに読まれて今のシーンで使われていない画像を、次のフレームの始めに解放する
///     （新しいシーンの Initialize で LoadTexture・Sprite 作成をした画像は残るので、共通の画像は読み直さない）
///   ・ReleaseUnused() を呼ぶと、使われていない画像を次のフレームの始めに全部解放する（重いシーンの途中で空けたいとき）
///   ・CreateOrUpdateTexture で作った画像（Text の文字など）は、使う物が 0 になったら次のフレームの始めに解放する
///   ・MarkPersistent した画像と "white"・"digitFont" は解放しない
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
	// 画像ファイルの中身（png・jpg などのバイト列）から読み込む（glTF の中に入っている画像用）。
	// name は他と重ならない名前にする（Acquire でこの名前を使う）。読み込み済みなら何もしない
	void LoadTextureFromMemory(const std::string& name, const uint8_t* data, size_t size);

	/// <summary>
	/// 1x1の白いテクスチャを内部で生成する
	/// </summary>
	void CreateInternalWhiteTexture();

	/// <summary>
	/// 数字("0"-"9")・"."・"-"・空白・アルファベット(A-Z)のグリフアトラス（"digitFont"）を内部で生成する。
	/// ビットパターンの定義はkBitmapFontGlyphsを参照。既に生成済みなら何もしない
	/// </summary>
	void CreateInternalDigitFontTexture();

	/// <summary>
	/// RGBA8(1ピクセル4バイト、上の行から順)のピクセル列から名前付きテクスチャを作成する。同名が既にあれば中身を差し替える
	/// （サイズが変わってもSRVの番号は変わらないので、Spriteは作り直さなくてよい）。
	/// テキスト描画や動的に生成する画像用。GPUへの転送を即時実行するため、描画中(PreDraw〜PostDraw)には呼ばないこと
	/// </summary>
	/// <returns>テクスチャ番号</returns>
	uint32_t CreateOrUpdateTexture(const std::string& name, uint32_t width, uint32_t height, const uint8_t* rgbaPixels);

	// 読み込み済みか
	bool HasTexture(const std::string& filePath) const;

	// 読み込み済み・作成済みの画像を使う印を受け取る（無ければ Fatal。先に LoadTexture する）
	TextureHandle Acquire(const std::string& filePath);
	// テクスチャ番号から使う印を受け取る
	TextureHandle AcquireByIndex(uint32_t textureIndex);
	// シーンが変わっても解放しない画像にする（タイトルからずっと使う UI の画像など）
	void MarkPersistent(const std::string& filePath);
	// 使われていない画像（永続を除く）を、次のフレームの始めに全部解放する
	void ReleaseUnused();
	// シーンが切り替わった（SceneManager が新しいシーンの Initialize の前に呼ぶ）
	void OnSceneChanged();
	// フレームの始めに呼ぶ（Framework）。解放を予約された画像を実際に解放する（前のフレームの描画は終わっているので安全）
	void Update();

	// 読み込んでいる画像の数（デバッグ表示用）
	uint32_t GetLoadedCount() const;

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

	friend class TextureHandle;

	struct TextureData {
		std::string filePath;
		DirectX::TexMetadata metadata;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource; // nullptr なら空き（解放済み）
		uint32_t srvIndex;
		uint32_t refCount = 0;
		// CreateOrUpdateTexture で作った（ファイルから読み直せない。使う物が 0 になったらすぐ解放）
		bool isDynamic = false;
		bool isPersistent = false;
		// 最後に読み込み・Acquire されたシーンの番号（シーン切り替え時の解放の判定用）
		uint64_t sceneSerial = 0;
	};
	// 番号（添字）は解放しても変わらない。解放した場所は freeSlots に入れて使い回す
	std::vector<TextureData> textureDatas;
	std::vector<uint32_t> freeSlots;
	std::unordered_map<std::string, uint32_t> indexByPath;

	// 今のシーンの番号（OnSceneChanged で増える）
	uint64_t currentSceneSerial = 0;
	// この番号より前のシーンで使われた、使う物 0 の画像を次の Update で解放する（0 なら予約なし）
	uint64_t releaseBeforeSerial = 0;
	bool isReleaseAllRequested = false;

	// 名前の画像の場所を確保して SRV 番号を取る（中身は呼んだ側が作る）
	uint32_t AllocateSlot(const std::string& name);
	// 読み込んだ画像に（DDS 以外は）ミップマップを作り、GPU に送って name で登録する
	void RegisterImage(const std::string& name, DirectX::ScratchImage& image, bool isDds);
	// 見つからなければ kNotFound
	static constexpr uint32_t kNotFound = UINT32_MAX;
	uint32_t FindIndex(const std::string& filePath) const;
	// 読み込まれている番号か確認する（解放済みなら Fatal）
	const TextureData& GetLoadedData(uint32_t textureIndex) const;
	void AddRef(uint32_t textureIndex);
	void Release(uint32_t textureIndex);
	void ReleaseSlot(uint32_t textureIndex);

	// コマンドリストを閉じて実行し、GPUの完了を待ってから次のフレーム用にリセットする（テクスチャ転送用）
	void ExecuteUploadAndWait();
};

