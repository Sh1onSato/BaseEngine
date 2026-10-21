#include "SpriteCommon.h"
#include "Sprite.h"
#include "Logger.h"
#include <algorithm>
using namespace ShaderCompiler;
void SpriteCommon::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
	this->dxCommon = dxCommon; // 受け取ったポインタをメンバ変数に保存
	this->srvManager = srvManager;
	// --- dxcCompilerの初期化 ---
	HRESULT hr = S_OK;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	Logger::CheckHResult(hr, "SpriteCommon: DxcCreateInstance(DxcUtils)");
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	Logger::CheckHResult(hr, "SpriteCommon: DxcCreateInstance(DxcCompiler)");
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	Logger::CheckHResult(hr, "SpriteCommon: CreateDefaultIncludeHandler");

	// --- Shaderのコンパイル（スプライト専用シェーダー。失敗時はCompileShader内でFatal） ---
	vertexShaderBlob = CompileShader(
		L"Resources/shaders/Sprite.VS.hlsl", // シェーダーのファイルパス
		L"vs_6_0", // プロファイル
		dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
	pixelShaderBlob = CompileShader(
		L"Resources/shaders/Sprite.PS.hlsl", // シェーダーのファイルパス
		L"ps_6_0", // プロファイル
		dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(), std::cout);
	if (vertexShaderBlob == nullptr || pixelShaderBlob == nullptr) {
		Logger::Fatal("SpriteCommon: failed to compile sprite shaders");
	}

	CreateRootSignature();

	// --- InputLayout ---
	inputElementDescs[0].SemanticName = "POSITION"; // セマンティクス名
	inputElementDescs[0].SemanticIndex = 0; // セマンティクスのインデックス
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // フォーマット
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT; // フォーマット
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputLayoutDesc.pInputElementDescs = inputElementDescs; // 入力要素の配列
	inputLayoutDesc.NumElements = _countof(inputElementDescs); // 入力要素の数

	// --- ブレンドモードごとにPSOを生成 ---
	for (size_t i = 0; i < kSpriteBlendModeCount; ++i) {
		CreatePipelineState(static_cast<SpriteBlendMode>(i));
	}

	// --- 描画キューの容量を先に確保しておく ---
	drawQueue.reserve(kInitialQueueCapacity);
}

void SpriteCommon::CreateRootSignature(){
	HRESULT hr;
	// --- 窓口（パラメータ）の設定 ---
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0; // t0
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b0: SpriteMaterial
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // b1: Transform(WVP)
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 1;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // t0: Texture
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	// --- サンプラーの設定（s0: リニア / s1: ポイント。UVスクロール用途のためWRAPのまま） ---
	D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
	const D3D12_FILTER filters[2] = { D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_POINT };
	for (uint32_t i = 0; i < _countof(staticSamplers); ++i) {
		staticSamplers[i].Filter = filters[i];
		staticSamplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSamplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSamplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSamplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		staticSamplers[i].MaxLOD = D3D12_FLOAT32_MAX;
		staticSamplers[i].ShaderRegister = i; // s0, s1
		staticSamplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

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
	if (FAILED(hr) && errorBlob) {
		Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
	}
	Logger::CheckHResult(hr, "SpriteCommon: D3D12SerializeRootSignature");
	hr = dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	Logger::CheckHResult(hr, "SpriteCommon: CreateRootSignature");
}

D3D12_RENDER_TARGET_BLEND_DESC SpriteCommon::MakeBlendDesc(SpriteBlendMode mode) {
	D3D12_RENDER_TARGET_BLEND_DESC desc{};
	// 全ての要素を書き込む
	desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	desc.BlendEnable = TRUE;
	desc.BlendOp = D3D12_BLEND_OP_ADD;
	// アルファチャンネルは全モード共通
	desc.SrcBlendAlpha = D3D12_BLEND_ONE;
	desc.DestBlendAlpha = D3D12_BLEND_ZERO;
	desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;

	switch (mode) {
	case SpriteBlendMode::kNone:
		desc.BlendEnable = FALSE;
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_ZERO;
		break;
	case SpriteBlendMode::kAdd: // 加算: Src*α + Dest
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		break;
	case SpriteBlendMode::kSubtract: // 減算: Dest - Src*α
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case SpriteBlendMode::kMultiply: // 乗算: Dest * Src
		desc.SrcBlend = D3D12_BLEND_ZERO;
		desc.DestBlend = D3D12_BLEND_SRC_COLOR;
		break;
	case SpriteBlendMode::kScreen: // スクリーン: Src*(1-Dest) + Dest
		desc.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		desc.DestBlend = D3D12_BLEND_ONE;
		break;
	case SpriteBlendMode::kNormal: // 通常: Src*α + Dest*(1-α)
	default:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		break;
	}
	return desc;
}

void SpriteCommon::CreatePipelineState(SpriteBlendMode mode) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature.Get(); // RootSignature
	desc.InputLayout = inputLayoutDesc; // 入力レイアウト
	desc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; // 頂点シェーダー
	desc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() }; // ピクセルシェーダー
	desc.BlendState.RenderTarget[0] = MakeBlendDesc(mode); // ブレンドステート

	// 裏面も表示し、三角形の中を塗りつぶす
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

	// 書き込むRTVの情報
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// 利用するトポロジ(形状)のタイプ。三角形
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように画面に色を打ち込むのかの設定
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 深度テスト・書き込みは行わない（ステンシルも使わない）
	desc.DepthStencilState.DepthEnable = false;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	HRESULT hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(
		&desc, IID_PPV_ARGS(&pipelineStates[static_cast<size_t>(mode)]));
	Logger::CheckHResult(hr, "SpriteCommon: CreateGraphicsPipelineState");
}

void SpriteCommon::PreDraw(){
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	// 1. スプライト専用の窓口(RootSignature)をセット
	commandList->SetGraphicsRootSignature(rootSignature.Get());

	// 2. 通常ブレンドのPSOをセット（ブレンドモードはここで毎回リセット）
	currentBlendMode = SpriteBlendMode::kNormal;
	commandList->SetPipelineState(pipelineStates[static_cast<size_t>(currentBlendMode)].Get());

	// 3. 形状を三角形に設定
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 4. デスクリプタヒープをセット
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvManager->GetDescriptorHeap() };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
}

void SpriteCommon::SetBlendMode(SpriteBlendMode mode) {
	if (mode == currentBlendMode || mode == SpriteBlendMode::kCount) {
		return;
	}
	dxCommon->GetCommandList()->SetPipelineState(pipelineStates[static_cast<size_t>(mode)].Get());
	currentBlendMode = mode;
}

void SpriteCommon::Enqueue(Sprite* sprite, uint32_t srvIndex) {
	if (sprite == nullptr) {
		return;
	}
	drawQueue.push_back({ sprite, srvIndex, sprite->GetLayer(), drawOrderCounter++ });
}

void SpriteCommon::Flush() {
	if (drawQueue.empty()) {
		return;
	}
	// 共通の描画設定（RootSignature・PSO・ヒープ等）をセット
	PreDraw();

	// レイヤーの小さい順に並べる（stable_sortなので同じレイヤー内はDrawを呼んだ順のまま）
	std::stable_sort(drawQueue.begin(), drawQueue.end(),
		[](const DrawRequest& a, const DrawRequest& b) { return a.layer < b.layer; });

	for (const DrawRequest& request : drawQueue) {
		request.sprite->RecordDrawCommand(request.srvIndex);
	}

	// キューを空にする（容量は残るので次フレームで再確保されない）
	drawQueue.clear();
	drawOrderCounter = 0;
}
