#include "SpriteCommon.h"
#include "Calculation.h"
using namespace ShaderCompiler;
void SpriteCommon::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
	this->dxCommon = dxCommon; // 受け取ったポインタをメンバ変数に保存
	this->srvManager = srvManager;
	// --- dxcCompilerの初期化 ---
	HRESULT hr = S_OK; // これを追加
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	// --- Shaderのコンパイル ---
	vertexShaderBlob = CompileShader(
		L"resources/shaders/Object3d.VS.hlsl", // シェーダーのファイルパス
		L"vs_6_0", // プロファイル
		dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
	assert(vertexShaderBlob != nullptr);

	pixelShaderBlob = CompileShader(
		L"resources/shaders/Object3d.PS.hlsl", // シェーダーのファイルパス
		L"ps_6_0", // プロファイル
		dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
	assert(pixelShaderBlob != nullptr);

	CreateRootSignature();
	// --- InputLayout、BlendState、RasterizerStateの設定 ---
	// InputLayout
	inputElementDescs[0].SemanticName = "POSITION"; // セマンティクス名
	inputElementDescs[0].SemanticIndex = 0; // セマンティクスのインデックス
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // フォーマット
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT; // フォーマット
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT; // フォーマット
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	// Object3d.VS.hlslのスキニング属性に追従（Spriteでは実データは持たないが、VertexDataが
	// デフォルト値を持つ集成体のため頂点バッファ側は無修正で動く。PSOの入力レイアウト検証のために宣言のみ必要）
	inputElementDescs[3].SemanticName = "BLENDWEIGHT";
	inputElementDescs[3].SemanticIndex = 0;
	inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[4].SemanticName = "BLENDINDICES";
	inputElementDescs[4].SemanticIndex = 0;
	inputElementDescs[4].Format = DXGI_FORMAT_R32G32B32A32_UINT;
	inputElementDescs[4].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputLayoutDesc.pInputElementDescs = inputElementDescs; // 入力要素の配列
	inputLayoutDesc.NumElements = _countof(inputElementDescs); // 入力要素の数

	// 全ての要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	// 裏面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// --- GraphicsPipelineStateの設定と生成 ---
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc; // 入力レイアウト
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),vertexShaderBlob->GetBufferSize() }; // 頂点シェーダー
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),pixelShaderBlob->GetBufferSize() }; // ピクセルシェーダー
	graphicsPipelineStateDesc.BlendState = blendDesc; // ブレンドステート
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc; // ラスタライザーステート

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1; // 書き込むRTVの数
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 書き込むRTVのフォーマット

	// 利用するトポロジ(形状)のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むのかの設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 深度ステンシルの設定
	depthStencilDesc.DepthEnable = false; // 深度テストを無効にする
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度値を書き込まない
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS; // 常に描画

	
	// ステンシルは使わない
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc; // パイプラインステートに深度ステンシルの設定を適用
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; // 深度ステンシルのフォーマットを設定

	// 実際に生成
	hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(
		&graphicsPipelineStateDesc, // パイプラインステートの設定
		IID_PPV_ARGS(&graphicsPipelineState) // パイプラインステートのポインタ
	);
	assert(SUCCEEDED(hr));

	// --- t2(gSkinMatrices)用のダミーパレット（単位行列1個）を作成 ---
	// Spriteはスキニングしないが、Object3d.VS.hlslを共用しているため常時何かをバインドする必要がある
	defaultBonePaletteResource = dxCommon->CreatBufferResource(sizeof(Matrix4x4));
	Matrix4x4* defaultBonePaletteData = nullptr;
	hr = defaultBonePaletteResource->Map(0, nullptr, reinterpret_cast<void**>(&defaultBonePaletteData));
	assert(SUCCEEDED(hr));
	*defaultBonePaletteData = Calculation::MakeIdentity4x4();
	defaultBonePaletteResource->Unmap(0, nullptr);
}

void SpriteCommon::CreateRootSignature(){
	HRESULT hr;
	// --- 窓口（パラメータ）の設定 ---
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0; // t0
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[9] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b0: Material
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b1: Transform
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 1;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // t0: Texture
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b2: DirectionalLight
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 2;

	// Object3d.PS.hlslを共用しているため、PointLight(b3)/SpotLight(b4)もPSOの
	// ルートシグネチャ検証を通すために宣言が必要（b2と同様、Spriteでは未バインドのまま）
	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b3: PointLight
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[4].Descriptor.ShaderRegister = 3;

	rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b4: SpotLight
	rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[5].Descriptor.ShaderRegister = 4;

	// Object3d.PS.hlslのシャドウマッピング追加(b5: ShadowData, t1: ShadowMap)にも
	// 同様にPSOのルートシグネチャ検証を通すためのダミー宣言が必要（Spriteでは未バインドのまま）
	rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b5: ShadowData
	rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[6].Descriptor.ShaderRegister = 5;

	D3D12_DESCRIPTOR_RANGE shadowDescriptorRange[1] = {};
	shadowDescriptorRange[0].BaseShaderRegister = 1; // t1
	shadowDescriptorRange[0].NumDescriptors = 1;
	shadowDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadowDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // t1: ShadowMap
	rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[7].DescriptorTable.pDescriptorRanges = shadowDescriptorRange;
	rootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(shadowDescriptorRange);

	// Object3d.VS.hlslのスキニング追加(t2: gSkinMatrices)にも同様にPSOのルートシグネチャ検証を
	// 通すためのダミー宣言が必要（Spriteは常に単位行列1個のデフォルトパレットをバインドする＝実質ノーオペレーション）
	rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; // t2: SkinMatrices
	rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[8].Descriptor.ShaderRegister = 2;

	// --- サンプラーの設定 ---
	D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0; // s0
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// s1: ShadowMap用の比較サンプラー（Object3d.PS.hlslのgShadowSamplerと一致させる）
	staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[1].ShaderRegister = 1; // s1
	staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// --- シリアライズと生成 ---
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	assert(SUCCEEDED(hr));
	hr = dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));
}

void SpriteCommon::PreDraw(){
	// 1. スプライト専用の窓口(RootSignature)をセット
	dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());

	// 2. スプライト専用のレシピ(PipelineState)をセット
	dxCommon->GetCommandList()->SetPipelineState(graphicsPipelineState.Get());

	// 3. 形状を三角形に設定
	dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 4. デスクリプタヒープをセット
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvManager->GetDescriptorHeap() };
	dxCommon->GetCommandList()->SetDescriptorHeaps(1, descriptorHeaps);

	// 5. t2(gSkinMatrices)に常時ダミーの単位行列パレットをバインド（実質ノーオペレーション）
	dxCommon->GetCommandList()->SetGraphicsRootShaderResourceView(8, defaultBonePaletteResource->GetGPUVirtualAddress());
}