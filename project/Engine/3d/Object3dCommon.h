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


    Microsoft::WRL::ComPtr <IDxcUtils> dxcUtils = nullptr;
    Microsoft::WRL::ComPtr <IDxcCompiler3> dxcCompiler = nullptr;
    Microsoft::WRL::ComPtr <IDxcIncludeHandler> includeHandler = nullptr;
    Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlob = nullptr;
    Microsoft::WRL::ComPtr < IDxcBlob> pixelShaderBlob = nullptr;
    Microsoft::WRL::ComPtr <IDxcBlob> shadowVertexShaderBlob = nullptr;

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
};

