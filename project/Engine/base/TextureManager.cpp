#include "TextureManager.h"
#include "Logger.h"
#include <algorithm>
#include <cstring>

TextureManager* TextureManager::instance = nullptr;

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

void TextureManager::CreateInternalWhiteTexture() {
	const std::string name = "white";
	// 既に存在するかチェック
	auto it = std::find_if(textureDatas.begin(), textureDatas.end(), [&](TextureData& d) { return d.filePath == name; });
	if (it != textureDatas.end()) return;

	// 1x1ピクセルの白色データを作成
	DirectX::ScratchImage image{};
	HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
	assert(SUCCEEDED(hr));

	// 白色を書き込む
	uint8_t* pixels = image.GetPixels();
	pixels[0] = 255; // R
	pixels[1] = 255; // G
	pixels[2] = 255; // B
	pixels[3] = 255; // A

	// テクスチャデータを追加
	textureDatas.resize(textureDatas.size() + 1);
	TextureData& textureData = textureDatas.back();

	textureData.filePath = name;
	textureData.metadata = image.GetMetadata();
	textureData.resource = CreateTextureResource(dxCommon->GetDevice(), textureData.metadata);

	// SRVの作成
	textureData.srvIndex = srvManager->Allocate();
	srvManager->CreateSRVForTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, 1);

	// アップロード
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureData.resource.Get(), image, dxCommon);

	// コマンド実行と待機
	hr = dxCommon->GetCommandList()->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* commandLists[] = { dxCommon->GetCommandList() };
	dxCommon->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon->WaitForGpu();

	hr = dxCommon->GetCommandAllocator()->Reset(); assert(SUCCEEDED(hr));
	hr = dxCommon->GetCommandList()->Reset(dxCommon->GetCommandAllocator(), nullptr); assert(SUCCEEDED(hr));
}

void TextureManager::CreateInternalDigitFontTexture() {
	const std::string name = "digitFont";
	// 既に存在するかチェック
	auto it = std::find_if(textureDatas.begin(), textureDatas.end(), [&](TextureData& d) { return d.filePath == name; });
	if (it != textureDatas.end()) return;

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
	assert(SUCCEEDED(hr));

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

	// テクスチャデータを追加
	textureDatas.resize(textureDatas.size() + 1);
	TextureData& textureData = textureDatas.back();

	textureData.filePath = name;
	textureData.metadata = image.GetMetadata();
	textureData.resource = CreateTextureResource(dxCommon->GetDevice(), textureData.metadata);

	// SRVの作成
	textureData.srvIndex = srvManager->Allocate();
	srvManager->CreateSRVForTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, 1);

	// アップロード
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureData.resource.Get(), image, dxCommon);

	// コマンド実行と待機
	hr = dxCommon->GetCommandList()->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* commandLists[] = { dxCommon->GetCommandList() };
	dxCommon->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon->WaitForGpu();

	hr = dxCommon->GetCommandAllocator()->Reset(); assert(SUCCEEDED(hr));
	hr = dxCommon->GetCommandList()->Reset(dxCommon->GetCommandAllocator(), nullptr); assert(SUCCEEDED(hr));
}

void TextureManager::LoadTexture(const std::string& filePath){
	// 読み込み済みテクスチャを検索
	auto it = std::find_if(
		textureDatas.begin(),
		textureDatas.end(),
		[&](TextureData& textureData) {
			return textureData.filePath == filePath;
		}
	);
	if(it!= textureDatas.end()){
		// 見つかった場合は何もしない
		return;
	}

	assert(textureDatas.size() < SrvManager::kMaxSRVCount);

	// テクスチャの読み込み
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr;
	if (filePath.find(".dds") != std::string::npos) {
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	} else {
		hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	}
	assert(SUCCEEDED(hr));

	// ミニマップの作成（DDSの場合は既に含まれていることが多いのでチェック）
	DirectX::ScratchImage mipChain{};
	if (DirectX::IsCompressed(image.GetMetadata().format) || filePath.find(".dds") != std::string::npos) {
		mipChain = std::move(image);
	} else {
		hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipChain);
		assert(SUCCEEDED(hr));
	}

	// テクスチャデータを追加
	textureDatas.resize(textureDatas.size() + 1);
	// 追加したテクスチャデータの参照を取得
	TextureData& textureData = textureDatas.back();

	textureData.filePath = filePath;
	textureData.metadata = mipChain.GetMetadata();
	textureData.resource = CreateTextureResource(dxCommon->GetDevice(), textureData.metadata);

	// SRVの作成
	textureData.srvIndex = srvManager->Allocate();
	if (textureData.metadata.miscFlags & DirectX::TEX_MISC_TEXTURECUBE) {
		srvManager->CreateSRVForTextureCube(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, static_cast<UINT>(textureData.metadata.mipLevels));
	} else {
		srvManager->CreateSRVForTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata.format, static_cast<UINT>(textureData.metadata.mipLevels));
	}

	// テクスチャデータのアップロード
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureData.resource.Get(),mipChain,dxCommon);


	// コマンドリストを閉じて実行、アップロード完了を待機
	hr = dxCommon->GetCommandList()->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* commandListsForUpload[] = { dxCommon->GetCommandList() };
	dxCommon->GetCommandQueue()->ExecuteCommandLists(1, commandListsForUpload);
	dxCommon->WaitForGpu();

	// コマンドリストとアロケーターをリセットして、メインループで再度使用できるようにする
	hr = dxCommon->GetCommandAllocator()->Reset(); assert(SUCCEEDED(hr));
	hr = dxCommon->GetCommandList()->Reset(dxCommon->GetCommandAllocator(), nullptr); assert(SUCCEEDED(hr));

}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath){
	auto it = std::find_if(
		textureDatas.begin(),
		textureDatas.end(),
		[&](TextureData& textureData) {
			return textureData.filePath == filePath;
		}
	);
	if(it!= textureDatas.end()) {
		uint32_t textureIndex = static_cast<uint32_t>(std::distance(textureDatas.begin(), it));
		return textureIndex;
	}
	assert(0);
	return 0;
}

uint32_t TextureManager::GetSrvIndex(uint32_t textureIndex) const {
	assert(textureIndex < textureDatas.size());
	return textureDatas[textureIndex].srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureIndex){
	assert(textureIndex < textureDatas.size());

	TextureData& textureData = textureDatas[textureIndex];
	return srvManager->GetGPUDescriptorHandle(textureData.srvIndex);
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
	assert(SUCCEEDED(hr));
	return resource;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureIndex){
	assert(textureIndex < textureDatas.size());
	
	TextureData& textureData = textureDatas[textureIndex];
	return textureData.metadata;
}

// uint32_t TextureManager::kSRVIndexTop = 1; // 削除