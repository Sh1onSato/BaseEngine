#pragma once
#include"DirectXCommon.h"
#include "Structs.h"
#include <wrl.h>
#include <d3d12.h>
#include"Calculation.h"
#include <dxcapi.h>
#include "SrvManager.h"

// Object3d描画時のブレンドモード
enum class Object3dBlendMode {
    kNormal,   // 通常（不透明、深度書き込みあり）
    kAdditive, // 加算合成（発光表現用、深度書き込みなし）
};

class Object3dCommon{
public:
	void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

	void PreDraw(Object3dBlendMode blendMode = Object3dBlendMode::kNormal);

    // シャドウマップ生成パス（ライト視点の深度のみ描画）
    void PreDrawShadow();
    void PostDrawShadow();

    // --- インスタンシング描画（ModelInstancer が使う） ---
    // 今のパス（PreDraw の通常パス / PreDrawShadow〜PostDrawShadow の影パス）に合ったインスタンシング用 PSO に切り替える
    void SetInstancedPipeline();
    // SetInstancedPipeline の前の PSO（通常の Object3d 用）に戻す
    void RestorePipeline();
    // PreDrawShadow〜PostDrawShadow の間なら true
    bool IsShadowPass() const { return isShadowPass; }
    // インスタンスデータ（t3、ルートSRV）を置くルートパラメータの番号（通常パス / 影パス）
    static constexpr UINT kRootIndexInstances = 9;
    static constexpr UINT kShadowRootIndexInstances = 2;
    // 材質（Model::Draw が部分ごとにセットする）: b6 の部分ごとの材質と、t4〜t7 の画像（法線・金属/粗さ・発光・影の順）
    static constexpr UINT kRootIndexSurfaceMaterial = 10;
    static constexpr UINT kRootIndexMaterialTextures = 11;
    static constexpr UINT kMaterialTextureCount = 4;
    // カメラの位置（b7）
    static constexpr UINT kRootIndexCamera = kRootIndexMaterialTextures + kMaterialTextureCount;
    static constexpr UINT kRootParameterCount = kRootIndexCamera + 1;

    DirectXCommon* GetDxCommon() const { return dxCommon; }

    // 非スキニングオブジェクト用のデフォルトボーンパレット（単位行列1個）のGPUアドレス
    // SetBonePalette()未設定のObject3dが、ルートSRV(t2)に必ず何かをバインドできるようにするためのフォールバック
    D3D12_GPU_VIRTUAL_ADDRESS GetDefaultBonePaletteGPUAddress() const { return defaultBonePaletteResource->GetGPUVirtualAddress(); }

    PointLight& GetPointLightData() { return *pointLightData; }
    SpotLight& GetSpotLightData() { return *spotLightData; }

    Matrix4x4 GetLightViewProjection() const { return shadowData->lightViewProjection; }
    float& GetShadowBias() { return shadowData->bias; }

    // シャドウの正射影範囲を外部（呼び出し側シーン）から設定する
    // lightDistance: 原点からライト位置までの距離 / orthoExtent: 正射影の片側半幅
    // nearClip / farClip: ライト視点のニア・ファークリップ
    // 未呼び出しの場合は従来値（40 / 55 / 0.1 / 80）のまま動作する
    void SetShadowOrthoRange(float lightDistance, float orthoExtent, float nearClip, float farClip);

    // シャドウマップの解像度（正方形）
    // ※HLSL側はcbuffer経由で渡せない静的定数として計算しているため、変更時は
    //   Resources\shaders\Object3d.PS.hlslのkShadowMapResolutionも同じ値に手動で合わせること
    static const uint32_t kShadowMapSize = 4096;
private:
    // ルートシグネチャの作成
    void CreateRootSignature();
    // グラフィックスパイプラインの作成
    void CreateGraphicsPipeline();
    // vertexShader で通常用・加算用の2つの PSO を作る（PS・入力レイアウト・ラスタライザ等は共通）
    void BuildMainPipelines(IDxcBlob* vertexShader, Microsoft::WRL::ComPtr<ID3D12PipelineState>& outNormal,
                            Microsoft::WRL::ComPtr<ID3D12PipelineState>& outAdditive);
    // vertexShader で深度だけ書く影用の PSO を作る
    void BuildShadowPipeline(IDxcBlob* vertexShader, Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPipeline);
    // シャドウ用ルートシグネチャの作成（b0のみ・VSのみの最小構成）
    void CreateShadowRootSignature();
    // シャドウ用パイプライン（深度専用）の作成
    void CreateShadowPipeline();
    // ライト視点の正射影ビュープロジェクション行列を再計算（シャドウパス開始前に呼ぶ）
    void UpdateLightViewProjection();

private:
    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateAdditive;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureShadow;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateShadow;

    // インスタンシング用（ルートシグネチャは通常用・影用と共通。VS だけ違う）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateInstanced;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateInstancedAdditive;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateShadowInstanced;
    // SetInstancedPipeline / RestorePipeline がどの PSO を選ぶかの判断用
    Object3dBlendMode currentBlendMode = Object3dBlendMode::kNormal;
    bool isShadowPass = false;


    Microsoft::WRL::ComPtr <IDxcUtils> dxcUtils = nullptr;
    Microsoft::WRL::ComPtr <IDxcCompiler3> dxcCompiler = nullptr;
    Microsoft::WRL::ComPtr <IDxcIncludeHandler> includeHandler = nullptr;
    Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlob = nullptr;
    Microsoft::WRL::ComPtr < IDxcBlob> pixelShaderBlob = nullptr;
    Microsoft::WRL::ComPtr <IDxcBlob> shadowVertexShaderBlob = nullptr;
    Microsoft::WRL::ComPtr <IDxcBlob> instancedVertexShaderBlob = nullptr;
    Microsoft::WRL::ComPtr <IDxcBlob> shadowInstancedVertexShaderBlob = nullptr;

    // ライト用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource;
    DirectionalLight* directionalLightData = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource;
    PointLight* pointLightData = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource;
    SpotLight* spotLightData = nullptr;

    // シャドウマップ用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowMapResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowDataResource;
    ShadowData* shadowData = nullptr;
    uint32_t shadowMapSrvIndex = 0;

    // シャドウの正射影範囲（デフォルトは従来のマジックナンバーと同値）
    float shadowLightDistance = 40.0f;
    float shadowOrthoExtent = 55.0f;
    float shadowNearClip = 0.1f;
    float shadowFarClip = 80.0f;

    // スキニング未使用のObject3d用デフォルトボーンパレット（単位行列1個ぶん）
    Microsoft::WRL::ComPtr<ID3D12Resource> defaultBonePaletteResource;

    // カメラの位置（PreDraw でアクティブなカメラから書く）
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource;
    CameraForGPU* cameraData = nullptr;
};

