#pragma once
#include"DirectXCommon.h"
#include"ShaderCompiler.h"
#include"SrvManager.h"

class SpriteCommon {
public:
	// 初期化
	void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

	// 描画前処理
	void PreDraw();

	// ゲッター
	DirectXCommon* GetDxCommon() const { return dxCommon; }
	IDxcBlob* GetVertexShaderBlob() const { return vertexShaderBlob.Get(); }
	IDxcBlob* GetPixelShaderBlob() const { return pixelShaderBlob.Get(); }
	// 画像番号を渡すと、その「住所(Handle)」を返してくれる関数を作る
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(uint32_t index) {
		return srvManager->GetGPUDescriptorHandle(index);
	}
private:
	DirectXCommon* dxCommon = nullptr;
	SrvManager* srvManager = nullptr;

	Microsoft::WRL::ComPtr <IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr <IDxcCompiler3> dxcCompiler = nullptr;
	Microsoft::WRL::ComPtr <IDxcIncludeHandler> includeHandler = nullptr;

	// RootSignatureを作るための専用関数
	void CreateRootSignature();
	// メンバ変数として保持する
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;

	// InputLayoutの設定（Object3d.VS/PS.hlslを使い回すため、スキニング属性(BLENDWEIGHT0/BLENDINDICES0)も追従させる）
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[5] = { };
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	// RasiterzerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	// GraphicsPipelineStateの設定
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	// DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	// 実際のパイプラインステート
	Microsoft::WRL::ComPtr <ID3D12PipelineState> graphicsPipelineState = nullptr;

	Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlob = nullptr;
	Microsoft::WRL::ComPtr < IDxcBlob> pixelShaderBlob = nullptr;

	// Object3d.VS.hlslのt2(gSkinMatrices)向けダミーパレット（単位行列1個）。
	// Spriteは常にこれをバインドすることで実質ノーオペレーションのスキニングになる。
	Microsoft::WRL::ComPtr<ID3D12Resource> defaultBonePaletteResource = nullptr;
};