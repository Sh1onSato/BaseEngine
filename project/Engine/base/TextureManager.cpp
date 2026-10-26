#include "TextureManager.h"
#include "Logger.h"
#include <algorithm>
#include <cstring>
#include <format>

TextureManager* TextureManager::instance = nullptr;

// ===== TextureHandle =====

TextureHandle::TextureHandle(const TextureHandle& other) : index(other.index) {
	if (IsValid() && TextureManager::instance) {
		TextureManager::instance->AddRef(index);
	}
}

TextureHandle& TextureHandle::operator=(const TextureHandle& other) {
	if (this != &other) {
		// 同じ画像のときに先に減らして 0 にならないよう、増やしてから減らす
		if (other.IsValid() && TextureManager::instance) {
			TextureManager::instance->AddRef(other.index);
		}
		Reset();
		index = other.index;
	}
	return *this;
}

TextureHandle::TextureHandle(TextureHandle&& other) noexcept : index(other.index) {
	other.index = kInvalidIndex;
}

TextureHandle& TextureHandle::operator=(TextureHandle&& other) noexcept {
	if (this != &other) {
		Reset();
		index = other.index;
		other.index = kInvalidIndex;
	}
	return *this;
}

void TextureHandle::Reset() {
	// エンジンの終了処理で TextureManager が先に消えていることがある
	if (IsValid() && TextureManager::instance) {
		TextureManager::instance->Release(index);
	}
	index = kInvalidIndex;
}

// ===== TextureManager =====

TextureManager* TextureManager::GetInstance() {
	if (instance == nullptr) {
		instance = new TextureManager();
	}
	return instance;
}

void TextureManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
	this->dxCommon = dxCommon;
	this->srvManager = srvManager;
	textureDatas.reserve(SrvManager::kMaxSRVCount);

	// 白テクスチャをデフォルトで作成
	CreateInternalWhiteTexture();
}

uint32_t TextureManager::AllocateSlot(const std::string& name) {
	uint32_t index = 0;
	if (!freeSlots.empty()) {
		index = freeSlots.back();
		freeSlots.pop_back();
		textureDatas[index] = TextureData{};
	} else {
		index = static_cast<uint32_t>(textureDatas.size());
		textureDatas.emplace_back();
	}
	TextureData& textureData = textureDatas[index];
	textureData.filePath = name;
	textureData.srvIndex = srvManager->Allocate();
	textureData.sceneSerial = currentSceneSerial;
	indexByPath[name] = index;
	return index;
}

uint32_t TextureManager::FindIndex(const std::string& filePath) const {
	auto it = indexByPath.find(filePath);
	return it != indexByPath.end() ? it->second : kNotFound;
}

void TextureManager::CreateInternalWhiteTexture() {
	const std::string name = "white";
	// 既に存在するかチェック
	if (FindIndex(name) != kNotFound) return;

	// 1x1ピクセルの白色データを作成
	DirectX::ScratchImage image{};
	HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
	Logger::CheckHResult(hr, "TextureManager: Initialize2D(white)");

	// 白色を書き込む
	uint8_t* pixels = image.GetPixels();
	pixels[0] = 255; // R
	pixels[1] = 255; // G
	pixels[2] = 255; // B
	pixels[3] = 255; // A

	// テクスチャデータを追加（エンジンのあちこちで使うので解放しない）
	TextureData& textureData = textureDatas[AllocateSlot(name)];
	textureData.isPersistent = true;
	textureData.metadata = image.GetMetadata();
	textureData.resource = CreateTextureResource(dxCommon->GetDevice(), textureData.metadata);

	// SRVの作成
	srvManager->CreateSRVForTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, 1);

	// アップロード
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureData.resource.Get(), image, dxCommon);

	// コマンド実行と待機
	ExecuteUploadAndWait();
}

void TextureManager::CreateInternalDigitFontTexture() {
	const std::string name = "digitFont";
	// 既に存在するかチェック
	if (FindIndex(name) != kNotFound) return;

	// 5x7ドットマトリクスのビットパターン（1=点灯）。文字とビットパターンの対応表本体は
	// TextureManager::kBitmapFontGlyphs（TextureManager.h）に一覧化してまとめてある。
	const int kGlyphCount = kBitmapFontGlyphCount;
	constexpr int kGlyphWidth = 5;
	constexpr int kGlyphHeight = 7;

	// 各グリフは7x9セル（5x7の実体＋周囲1pxの透明パディング）に配置。
	// パディングはサンプラーが線形補間のため、隣接グリフのにじみ（バイリニアブリーディング）を防ぐ目的
	constexpr int kCellWidth = kGlyphWidth + 2;
	constexpr int kCellHeight = kGlyphHeight + 2;
	const int width = kCellWidth * kGlyphCount;
	const int height = kCellHeight;

	DirectX::ScratchImage image{};
	HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	Logger::CheckHResult(hr, "TextureManager: Initialize2D(digit font)");

	uint8_t* pixels = image.GetPixels();
	std::memset(pixels, 0, static_cast<size_t>(width) * height * 4); // 全体を透明で初期化

	for (int g = 0; g < kGlyphCount; ++g) {
		for (int row = 0; row < kGlyphHeight; ++row) {
			for (int col = 0; col < kGlyphWidth; ++col) {
				bool lit = (kBitmapFontGlyphs[g].rows[row] >> (kGlyphWidth - 1 - col)) & 1;
				if (!lit) continue;
				int px = g * kCellWidth + 1 + col; // 1pxパディング
				int py = 1 + row;
				int idx = (py * width + px) * 4;
				pixels[idx + 0] = 255;
				pixels[idx + 1] = 255;
				pixels[idx + 2] = 255;
				pixels[idx + 3] = 255;
			}
		}
	}

	// テクスチャデータを追加（BitmapText が使い回すので解放しない）
	TextureData& textureData = textureDatas[AllocateSlot(name)];
	textureData.isPersistent = true;
	textureData.metadata = image.GetMetadata();
	textureData.resource = CreateTextureResource(dxCommon->GetDevice(), textureData.metadata);

	// SRVの作成
	srvManager->CreateSRVForTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, 1);

	// アップロード
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureData.resource.Get(), image, dxCommon);

	// コマンド実行と待機
	ExecuteUploadAndWait();
}

void TextureManager::LoadTexture(const std::string& filePath){
	// 読み込み済みテクスチャを検索
	if (uint32_t found = FindIndex(filePath); found != kNotFound) {
		// 見つかった場合は読み直さない（このシーンで使う印だけ付けて、シーン切り替えで解放されないようにする）
		textureDatas[found].sceneSerial = currentSceneSerial;
		return;
	}

	// テクスチャの読み込み
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr;
	if (filePath.find(".dds") != std::string::npos) {
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	} else {
		hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	}
	Logger::CheckHResult(hr, "TextureManager: Load texture file(" + filePath + ")");

	// ミニマップの作成（DDSの場合は既に含まれていることが多いのでチェック）
	DirectX::ScratchImage mipChain{};
	if (DirectX::IsCompressed(image.GetMetadata().format) || filePath.find(".dds") != std::string::npos) {
		mipChain = std::move(image);
	} else {
		hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipChain);
		Logger::CheckHResult(hr, "TextureManager: GenerateMipMaps(" + filePath + ")");
	}

	// テクスチャデータを追加
	TextureData& textureData = textureDatas[AllocateSlot(filePath)];
	textureData.metadata = mipChain.GetMetadata();
	textureData.resource = CreateTextureResource(dxCommon->GetDevice(), textureData.metadata);

	// SRVの作成
	if (textureData.metadata.miscFlags & DirectX::TEX_MISC_TEXTURECUBE) {
		srvManager->CreateSRVForTextureCube(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, static_cast<UINT>(textureData.metadata.mipLevels));
	} else {
		srvManager->CreateSRVForTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, static_cast<UINT>(textureData.metadata.mipLevels));
	}

	// テクスチャデータのアップロード
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureData.resource.Get(),mipChain,dxCommon);


	// コマンドリストを閉じて実行、アップロード完了を待機。リセットしてメインループで再度使用できるようにする
	ExecuteUploadAndWait();

}

uint32_t TextureManager::CreateOrUpdateTexture(const std::string& name, uint32_t width, uint32_t height, const uint8_t* rgbaPixels) {
	assert(width > 0 && height > 0);

	DirectX::ScratchImage image{};
	HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	Logger::CheckHResult(hr, "TextureManager: Initialize2D(" + name + ")");
	std::memcpy(image.GetPixels(), rgbaPixels, static_cast<size_t>(width) * height * 4);

	uint32_t index = FindIndex(name);
	bool isNew = index == kNotFound;
	if (isNew) {
		index = AllocateSlot(name);
		// 作った本人（Text など）しか使わないので、使う物が 0 になったらすぐ解放する
		textureDatas[index].isDynamic = true;
	}
	TextureData& textureData = textureDatas[index];

	bool sameSize = !isNew && textureData.metadata.width == width && textureData.metadata.height == height;
	if (sameSize) {
		// 同じサイズなら既存のリソースに上書きする（UploadTextureDataはCOPY_DEST前提なので戻しておく）
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = textureData.resource.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		dxCommon->GetCommandList()->ResourceBarrier(1, &barrier);
	} else {
		// サイズが変わる場合は作り直し、同じSRV番号にビューを作り直す
		// （前フレームの描画はPostDraw()でGPU完了を待っているので、古いリソースを解放しても安全）
		textureData.metadata = image.GetMetadata();
		textureData.resource = CreateTextureResource(dxCommon->GetDevice(), textureData.metadata);
		srvManager->CreateSRVForTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, 1);
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureData.resource.Get(), image, dxCommon);
	ExecuteUploadAndWait();

	return index;
}

bool TextureManager::HasTexture(const std::string& filePath) const {
	return FindIndex(filePath) != kNotFound;
}

TextureHandle TextureManager::Acquire(const std::string& filePath) {
	uint32_t index = GetTextureIndexByFilePath(filePath);
	textureDatas[index].sceneSerial = currentSceneSerial;
	AddRef(index);
	return TextureHandle(index);
}

TextureHandle TextureManager::AcquireByIndex(uint32_t textureIndex) {
	GetLoadedData(textureIndex);
	textureDatas[textureIndex].sceneSerial = currentSceneSerial;
	AddRef(textureIndex);
	return TextureHandle(textureIndex);
}

void TextureManager::MarkPersistent(const std::string& filePath) {
	textureDatas[GetTextureIndexByFilePath(filePath)].isPersistent = true;
}

void TextureManager::ReleaseUnused() {
	isReleaseAllRequested = true;
}

void TextureManager::OnSceneChanged() {
	++currentSceneSerial;
	releaseBeforeSerial = currentSceneSerial;
}

void TextureManager::Update() {
	for (uint32_t i = 0; i < textureDatas.size(); ++i) {
		const TextureData& textureData = textureDatas[i];
		if (!textureData.resource || textureData.refCount > 0 || textureData.isPersistent) {
			continue;
		}
		const bool isFromPreviousScene = releaseBeforeSerial != 0 && textureData.sceneSerial < releaseBeforeSerial;
		if (textureData.isDynamic || isReleaseAllRequested || isFromPreviousScene) {
			ReleaseSlot(i);
		}
	}
	isReleaseAllRequested = false;
	releaseBeforeSerial = 0;
}

uint32_t TextureManager::GetLoadedCount() const {
	return static_cast<uint32_t>(std::count_if(textureDatas.begin(), textureDatas.end(),
		[](const TextureData& textureData) { return textureData.resource != nullptr; }));
}

void TextureManager::AddRef(uint32_t textureIndex) {
	assert(textureIndex < textureDatas.size() && textureDatas[textureIndex].resource);
	++textureDatas[textureIndex].refCount;
}

void TextureManager::Release(uint32_t textureIndex) {
	assert(textureIndex < textureDatas.size() && textureDatas[textureIndex].refCount > 0);
	// 0 になってもここでは解放しない（描画中かもしれないので、次のフレームの始めの Update で解放する）
	--textureDatas[textureIndex].refCount;
}

void TextureManager::ReleaseSlot(uint32_t textureIndex) {
	TextureData& textureData = textureDatas[textureIndex];
	srvManager->Free(textureData.srvIndex);
	indexByPath.erase(textureData.filePath);
	textureData = TextureData{}; // resource も解放される
	freeSlots.push_back(textureIndex);
}

void TextureManager::ExecuteUploadAndWait() {
	HRESULT hr = dxCommon->GetCommandList()->Close();
	Logger::CheckHResult(hr, "TextureManager: CommandList Close");
	ID3D12CommandList* commandLists[] = { dxCommon->GetCommandList() };
	dxCommon->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon->WaitForGpu();

	hr = dxCommon->GetCommandAllocator()->Reset();
	Logger::CheckHResult(hr, "TextureManager: CommandAllocator Reset");
	hr = dxCommon->GetCommandList()->Reset(dxCommon->GetCommandAllocator(), nullptr);
	Logger::CheckHResult(hr, "TextureManager: CommandList Reset");
}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath){
	uint32_t index = FindIndex(filePath);
	if (index != kNotFound) {
		return index;
	}
	// 見つからない＝LoadTexture忘れ（か、前のシーンで読んだ画像が解放された）。
	// Releaseで黙って0番を返すと別の画像が出て原因が分かりにくいので止める
	Logger::Fatal("TextureManager: texture not loaded (" + filePath + "). Call LoadTexture first");
}

const TextureManager::TextureData& TextureManager::GetLoadedData(uint32_t textureIndex) const {
	if (textureIndex >= textureDatas.size() || !textureDatas[textureIndex].resource) {
		Logger::Fatal(std::format("TextureManager: released texture used (index {})", textureIndex));
	}
	return textureDatas[textureIndex];
}

uint32_t TextureManager::GetSrvIndex(uint32_t textureIndex) const {
	return GetLoadedData(textureIndex).srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureIndex){
	return srvManager->GetGPUDescriptorHandle(GetLoadedData(textureIndex).srvIndex);
}

void TextureManager::Finalize() {
	delete instance;
	instance = nullptr;
}


Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
	// テクスチャのリソースを作成
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	// 利用するHeepの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// Resourceの生成
	Microsoft::WRL::ComPtr <ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	Logger::CheckHResult(hr, "TextureManager: CreateCommittedResource(texture)");
	return resource;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureIndex){
	return GetLoadedData(textureIndex).metadata;
}
