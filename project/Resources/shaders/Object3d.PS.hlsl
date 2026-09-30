#include"object3d.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    float2 padding;
};

struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float padding;
};

struct ShadowData
{
    float4x4 lightViewProjection;
    float bias;
    float orthoExtentWorld;     // 正射影の片側半幅（C++側Object3dCommon::SetShadowOrthoRange()で設定）
    float orthoDepthRangeWorld; // ファークリップ - ニアクリップ
    float padding;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

ConstantBuffer<DirectionalLight> gDirectionLight : register(b2); // b1レジスタはC++側と一致させる
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);
ConstantBuffer<ShadowData> gShadowData : register(b5);

Texture2D<float> gShadowMap : register(t1);
SamplerComparisonState gShadowSampler : register(s1);

// ディレクショナルライトのシャドウ係数を計算する（1.0=影なし、0.0=完全に影）
// PCSS(Percentage-Closer Soft Shadows)風に、遮蔽物と受光面の距離が離れているほど影の輪郭をぼかす
// ※HLSLの制約上cbuffer経由の値から静的定数を導出できないため直書き。変更時はC++側
//   Object3dCommon::kShadowMapSizeも同じ値に手動で合わせること
static const float kShadowMapResolution = 4096.0f;
static const float kShadowMapTexelSize = 1.0f / kShadowMapResolution;
// 法線方向のオフセット量（壁のように光に対してほぼ真横を向く面でのシャドウアクネ対策）
static const float kNormalOffsetScale = 0.05f;
// 正射影の全幅・深度レンジ（ワールド距離⇔UV距離の換算に使用）は
// C++側Object3dCommon::SetShadowOrthoRange()から定数バッファ(gShadowData)経由で渡される
// 影のぼかしやすさ（大きいほど、遮蔽物から受光面までの距離に対してペナンブラが広がりやすい）
static const float kPenumbraSlope = 0.3f;
static const float kMinFilterRadiusUV = kShadowMapTexelSize;      // 接触影は硬く保つための下限
static const float kMaxFilterRadiusUV = kShadowMapTexelSize * 13.0f; // ボケすぎ・負荷増大の防止用の上限

static const int kPcssSampleCount = 16;
static const float2 kPoissonDisk[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

// 受光点周辺を探索し、光源側にある遮蔽物(receiverZより手前=値が小さい)の平均深度を求める
// SampleCmpLevelZeroは比較結果(0/1)しか返さないため、生の深度が必要なここではLoadで直接テクセルを読む
float SearchAverageBlockerDepth(float2 shadowUV, float receiverZ, out int blockerCount)
{
    float blockerSum = 0.0f;
    blockerCount = 0;
    [unroll]
    for (int i = 0; i < kPcssSampleCount; ++i) {
        float2 offsetUV = shadowUV + kPoissonDisk[i] * kMaxFilterRadiusUV;
        int2 texel = clamp(int2(offsetUV * kShadowMapResolution), int2(0, 0), int2(kShadowMapResolution - 1, kShadowMapResolution - 1));
        float sampleDepth = gShadowMap.Load(int3(texel, 0));
        if (sampleDepth < receiverZ) {
            blockerSum += sampleDepth;
            blockerCount += 1;
        }
    }
    return (blockerCount > 0) ? (blockerSum / blockerCount) : 0.0f;
}

float CalculateDirectionalShadow(float3 worldPosition, float3 normal, float NdotL)
{
    // 法線方向に少しずらしてからライト空間へ変換することで、グレージング角での自己遮蔽誤判定を抑える
    float3 offsetWorldPosition = worldPosition + normal * kNormalOffsetScale;
    float4 lightClipPos = mul(float4(offsetWorldPosition, 1.0f), gShadowData.lightViewProjection);
    float3 ndc = lightClipPos.xyz / lightClipPos.w;

    float2 shadowUV = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);

    // ライトの正射影範囲外は影なし扱い（誤って影が落ちるのを防ぐ）
    if (shadowUV.x < 0.0f || shadowUV.x > 1.0f || shadowUV.y < 0.0f || shadowUV.y > 1.0f) {
        return 1.0f;
    }

    // 光に対して面が斜めになるほどバイアスを強める（スロープスケールバイアス）
    float slopeScaledBias = gShadowData.bias * max(1.0f, (1.0f - saturate(NdotL)) * 4.0f);
    float receiverZ = ndc.z - slopeScaledBias;

    // 1. ブロッカー探索：遮蔽物が見つからなければ影なし（直交投影なので探索半径は深度によらず固定でよい）
    int blockerCount = 0;
    float avgBlockerZ = SearchAverageBlockerDepth(shadowUV, receiverZ, blockerCount);
    if (blockerCount == 0) {
        return 1.0f;
    }

    // 2. ペナンブラ幅の推定：遮蔽物と受光面のギャップ(ワールド距離)が大きいほど輪郭を広くぼかす
    //    正射影(平行光線)は遠近除算が不要なため、ギャップ距離に比例させるだけでよい
    float gapWorld = (receiverZ - avgBlockerZ) * gShadowData.orthoDepthRangeWorld;
    float penumbraWorld = kPenumbraSlope * gapWorld;
    float filterRadiusUV = clamp(penumbraWorld / (2.0f * gShadowData.orthoExtentWorld), kMinFilterRadiusUV, kMaxFilterRadiusUV);

    // 3. 可変半径のPCF（ペナンブラ幅に応じてサンプリング範囲を広げ、ぼかし具合を距離に応じて変える）
    float shadowFactor = 0.0f;
    [unroll]
    for (int j = 0; j < kPcssSampleCount; ++j) {
        float2 offsetUV = kPoissonDisk[j] * filterRadiusUV;
        shadowFactor += gShadowMap.SampleCmpLevelZero(gShadowSampler, shadowUV + offsetUV, receiverZ);
    }
    shadowFactor /= (float)kPcssSampleCount;

    return shadowFactor;
}

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};


PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord,0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    // 基本色を設定
    output.color = gMaterial.color * textureColor;

    // アルファ値が0.5以下ならピクセルを破棄（2値抜き）
    if (output.color.a <= 0.5f) {
        discard;
    }

    // ライティングの計算と適用
    if (gMaterial.enableLighting != 0){
        float3 baseColor = output.color.rgb;
        float3 normal = normalize(input.normal);
        float3 lightingResult = float3(0.0f, 0.0f, 0.0f);

        // ディレクショナルライト（シャドウマップ + PCFで影を落とす）
        {
            float NdotL = dot(normal, -gDirectionLight.direction);
            float cosFactor = pow(NdotL * 0.5f + 0.5f, 2.0f);
            float shadowFactor = CalculateDirectionalShadow(input.worldPosition, normal, NdotL);
            lightingResult += gDirectionLight.color.rgb * gDirectionLight.intensity * cosFactor * shadowFactor;
        }

        // 点光源
        {
            float3 toLight = gPointLight.position - input.worldPosition;
            float distance = length(toLight);
            float3 lightDirection = toLight / distance;
            float NdotL = dot(normal, lightDirection);
            float cosFactor = pow(NdotL * 0.5f + 0.5f, 2.0f);
            float attenuation = pow(saturate(-distance / gPointLight.radius + 1.0f), gPointLight.decay);
            lightingResult += gPointLight.color.rgb * gPointLight.intensity * cosFactor * attenuation;
        }

        // スポットライト
        {
            float3 toLight = gSpotLight.position - input.worldPosition;
            float distance = length(toLight);
            float3 lightDirection = toLight / distance;
            float NdotL = dot(normal, lightDirection);
            float cosFactor = pow(NdotL * 0.5f + 0.5f, 2.0f);
            float attenuation = pow(saturate(-distance / gSpotLight.distance + 1.0f), gSpotLight.decay);
            float cosAngle = dot(-lightDirection, gSpotLight.direction);
            float falloffFactor = saturate((cosAngle - gSpotLight.cosAngle) / (gSpotLight.cosFalloffStart - gSpotLight.cosAngle));
            lightingResult += gSpotLight.color.rgb * gSpotLight.intensity * cosFactor * attenuation * falloffFactor;
        }

        // 環境光（どの光源も直接当たらない面が完全な黒潰れにならないよう底上げする）
        static const float kAmbientIntensity = 0.08f;
        float3 ambient = baseColor * kAmbientIntensity;

        output.color.rgb = baseColor * lightingResult + ambient;
    }

    return output;
}