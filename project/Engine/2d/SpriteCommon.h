#pragma once
#include"DirectXCommon.h"
#include"ShaderCompiler.h"
#include"SrvManager.h"
#include"SpriteBlendMode.h"
#include <vector>
#include <cstdint>

class Sprite;
class Camera2D;
struct Rect;

// スプライト共通部
// 【描画の流れ（レイヤー対応）】
// ・Sprite::Draw は描画キューに積むだけ。シーンの Draw が終わると SceneManager が Flush する。
// ・Flush ではレイヤーの小さい順（同じレイヤー内は Draw を呼んだ順）に描画される。
// ・3Dより奥に出したいSpriteは、それを Draw した後に spriteCommon->Flush() を呼んでから3Dを描く。
// ・同じSpriteを1フレームに2回Drawしても定数バッファは1つなので、
//   最後のUpdateの位置で2回描かれる（今までと同じ制限）。
class SpriteCommon {
public:
	// 初期化
	void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

	// 描画前処理（ブレンドモードはkNormalにリセットされる）
	// ※ Flush 内で自動的に呼ばれるので、通常は呼ばなくてよい（呼んでも害はない）
	void PreDraw();

	// 描画キューに積む（Sprite::Draw から呼ばれる）
	void Enqueue(Sprite* sprite, uint32_t srvIndex);
	// 積まれたスプライトをレイヤー順に描画してキューを空にする
	void Flush();

	// 描く範囲を矩形（ゲーム座標）の中だけにする（スクロールリストの外にはみ出た部分を描かない用）。
	// Push してから Pop するまでに Draw したスプライトに効く（Flush の時点ではなく Draw を呼んだ時点の範囲）。
	// 入れ子にすると、重なった部分だけになる。Push と Pop は必ず同じ数だけ呼ぶこと
	void PushClipRect(const Rect& rect);
	void PopClipRect();

	// ブレンドモードを切り替える（現在と同じなら何もしない）
	void SetBlendMode(SpriteBlendMode mode);

	// 2Dカメラをセットする（所有しない。nullptrでカメラなし）
	// SetUseCamera(true) のSpriteだけが影響を受ける。
	// ※ カメラを持つシーンが破棄される前に必ず nullptr に戻すこと（ダングリング防止）
	void SetCamera2D(Camera2D* camera) { camera2D = camera; }
	Camera2D* GetCamera2D() const { return camera2D; }

	// ゲッター
	DirectXCommon* GetDxCommon() const { return dxCommon; }
	// 画像番号を渡すと、その「住所(Handle)」を返してくれる関数を作る
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(uint32_t index) {
		return srvManager->GetGPUDescriptorHandle(index);
	}
private:
	DirectXCommon* dxCommon = nullptr;
	SrvManager* srvManager = nullptr;
	// 2Dカメラ（所有しない）
	Camera2D* camera2D = nullptr;

	Microsoft::WRL::ComPtr <IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr <IDxcCompiler3> dxcCompiler = nullptr;
	Microsoft::WRL::ComPtr <IDxcIncludeHandler> includeHandler = nullptr;

	// RootSignatureを作るための専用関数
	void CreateRootSignature();
	// 指定ブレンドモードのPSOを生成する
	void CreatePipelineState(SpriteBlendMode mode);
	// ブレンドモードに対応するブレンド設定を返す
	static D3D12_RENDER_TARGET_BLEND_DESC MakeBlendDesc(SpriteBlendMode mode);

	// メンバ変数として保持する
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;

	// InputLayoutの設定（Sprite.VS.hlsl: POSITION0 / TEXCOORD0）
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = { };
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};

	// ブレンドモードごとのパイプラインステート
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates[kSpriteBlendModeCount];
	// 現在コマンドリストにセットしているブレンドモード
	SpriteBlendMode currentBlendMode = SpriteBlendMode::kNormal;

	Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlob = nullptr;
	Microsoft::WRL::ComPtr <IDxcBlob> pixelShaderBlob = nullptr;

	// 描く範囲（ゲーム座標）。Flush のときに今の描画先のピクセルに直す（内部解像度を下げた描画先でも合うように）
	struct ClipArea {
		float left;
		float top;
		float right;
		float bottom;
	};
	// 描画キューの1要素
	struct DrawRequest {
		Sprite* sprite;    // 描画するスプライト
		uint32_t srvIndex; // 使うテクスチャのSRVインデックス
		int layer;         // 積んだ時点のレイヤー
		uint32_t order;    // 積んだ順番（デバッグ用）
		bool hasClip;      // 積んだ時点で PushClipRect されていたか
		ClipArea clipArea; // そのときの描く範囲
	};
	// ゲーム座標の範囲を、今の描画先のピクセルの矩形にする（画面の外にはみ出た分は切る）
	D3D12_RECT ToScissorRect(const ClipArea& area) const;
	// PushClipRect された範囲（後ろほど内側で、外側と重なった部分にしてある）
	std::vector<ClipArea> clipStack;
	// 描画キュー
	std::vector<DrawRequest> drawQueue;
	// 積んだ順番のカウンタ（Flushでリセット）
	uint32_t drawOrderCounter = 0;
	// 初期確保するキューの容量
	static constexpr size_t kInitialQueueCapacity = 256;
};
