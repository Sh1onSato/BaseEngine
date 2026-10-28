#include"object3d.hlsli"

// 物ごとの材質（C++ の Material と同じ並び）
struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
    float4 emissive;             // 自分で光る色（rgb。モデルの発光に足す）
    float metallicOverride;      // 0 以上ならモデルの値の代わりに使う
    float roughnessOverride;
    int lightingModelOverride;   // -1 = モデルのまま、0 = 簡単な当たり方、1 = 金属・粗さを使う当たり方
    float padding2;
};

// モデルの部分ごとの材質（C++ の SurfaceMaterial と同じ並び）
struct SurfaceMaterial
{
    float4 emissive;
    float metallic;
    float roughness;
    float normalStrength;
    float occlusionStrength;
    uint flags;
    float3 padding;
};

struct Camera
{
    float3 worldPosition;
    float padding;
};

// SurfaceMaterial::flags（C++ の SurfaceMaterialFlags と同じ）
static const uint kFlagNormalMap = 1u << 0;
static const uint kFlagMetallicRoughnessMap = 1u << 1;
static const uint kFlagEmissiveMap = 1u << 2;
static const uint kFlagOcclusionMap = 1u << 3;
static const uint kFlagPhysicalLighting = 1u << 4;

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

// 材質の画像（使わない物には白が入っている。flags で使うかを決める）。t2・t3 は頂点シェーダーが使っている
ConstantBuffer<SurfaceMaterial> gSurface : register(b6);
ConstantBuffer<Camera> gCamera : register(b7);
Texture2D<float4> gNormalMap : register(t4);
Texture2D<float4> gMetallicRoughnessMap : register(t5);
Texture2D<float4> gEmissiveMap : register(t6);
Texture2D<float4> gOcclusionMap : register(t7);

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

// ===== 材質 =====
static const float kPi = 3.14159265f;
// 環境光（どの光源も直接当たらない面が完全な黒潰れにならないよう底上げする）
static const float kAmbientIntensity = 0.08f;
// 金属・粗さを使う当たり方の、周りの景色の照り返しの代わり（空の明るさの目安。金属が真っ黒にならないように）
static const float kAmbientSpecularIntensity = 0.3f;
// 粗さの下限（0 だと光が点になって見えなくなる・ちらつく）
static const float kMinRoughness = 0.04f;

// 法線マップで法線を曲げる。画面上での位置と UV の変化から「u が増える向き・v が増える向き」を作るので、頂点に接線は要らない。
// 法線マップは glTF・Blender と同じ並び（赤 = u の向き、緑 = 画像の上 = v が減る向き、青 = 面から外向き）
float3 ApplyNormalMap(float3 normal, float3 worldPosition, float2 uv, float3 mapValue, float strength)
{
    float3 dpdx = ddx(worldPosition);
    float3 dpdy = ddy(worldPosition);
    float2 duvdx = ddx(uv);
    float2 duvdy = ddy(uv);
    float3 dpdyPerp = cross(dpdy, normal);
    float3 dpdxPerp = cross(normal, dpdx);
    float3 tangent = dpdyPerp * duvdx.x + dpdxPerp * duvdy.x;
    float3 bitangent = dpdyPerp * duvdx.y + dpdxPerp * duvdy.y;
    float lengthSquared = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (lengthSquared <= 1e-20f) {
        return normal; // UV が付いていない（全部同じ UV）
    }
    float scale = rsqrt(lengthSquared);
    float3 local = mapValue * 2.0f - 1.0f;
    local.xy *= strength;
    return normalize(tangent * scale * local.x - bitangent * scale * local.y + normal * local.z);
}

// 光が細かい凹凸でどれだけこちらへ跳ね返るか（GGX）
float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

// 細かい凹凸が互いに光を遮る分（Smith / Schlick-GGX）
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float gv = NdotV / (NdotV * (1.0f - k) + k);
    float gl = NdotL / (NdotL * (1.0f - k) + k);
    return gv * gl;
}

// 斜めから見るほど強く反射する（フレネル。Schlick の近似）
float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - cosTheta, 5.0f);
}

// 金属・粗さを使う当たり方で、光1つ分の明るさ。radiance は光の色 × 強さ × 減衰
float3 PhysicalLight(float3 normal, float3 toCamera, float3 toLight, float3 radiance, float3 baseColor, float metallic, float roughness)
{
    float NdotL = saturate(dot(normal, toLight));
    if (NdotL <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float NdotV = max(dot(normal, toCamera), 1e-4f);
    float3 halfVector = normalize(toCamera + toLight);
    float NdotH = saturate(dot(normal, halfVector));
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, toCamera)), f0);
    float3 specular = DistributionGGX(NdotH, roughness) * GeometrySmith(NdotV, NdotL, roughness) * fresnel / (4.0f * NdotV * NdotL + 1e-4f);
    float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor;
    // ライトの強さは「π を掛けた単位」とする（今までの簡単な当たり方と、同じ光で同じくらいの明るさになるように）
    return (diffuse + specular * kPi) * radiance * NdotL;
}

// 周りの景色の照り返しの近似（景色の画像を使わない。Karis の式）
float3 EnvironmentBRDF(float3 f0, float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord,0.0f, 1.0f), gMaterial.uvTransform);
    float2 uv = transformedUV.xy;
    float4 textureColor = gTexture.Sample(gSampler, uv);

    // 基本色を設定
    output.color = gMaterial.color * textureColor * input.instanceColor;

    // アルファ値が0.5以下ならピクセルを破棄（2値抜き）
    if (output.color.a <= 0.5f) {
        discard;
    }

    // 自分で光る色（ライトが無くても見える。明るければ画面の効果のブルームで光って見える）
    float3 emissive = gSurface.emissive.rgb + gMaterial.emissive.rgb;
    if (gSurface.flags & kFlagEmissiveMap) {
        emissive *= gEmissiveMap.Sample(gSampler, uv).rgb;
    }

    // ライティングの計算と適用
    if (gMaterial.enableLighting != 0){
        float3 baseColor = output.color.rgb;
        float3 normal = normalize(input.normal);
        if (gSurface.flags & kFlagNormalMap) {
            normal = ApplyNormalMap(normal, input.worldPosition, uv, gNormalMap.Sample(gSampler, uv).rgb, gSurface.normalStrength);
        }
        // 影になりやすい所（くぼみ・すき間）は周りの光が届きにくい
        float occlusion = 1.0f;
        if (gSurface.flags & kFlagOcclusionMap) {
            occlusion = lerp(1.0f, gOcclusionMap.Sample(gSampler, uv).r, gSurface.occlusionStrength);
        }

        bool isPhysical = gMaterial.lightingModelOverride == 1 ||
                          (gMaterial.lightingModelOverride < 0 && (gSurface.flags & kFlagPhysicalLighting) != 0);
        if (isPhysical) {
            // --- 金属・粗さを使う当たり方 ---
            float metallic = gSurface.metallic;
            float roughness = gSurface.roughness;
            if (gSurface.flags & kFlagMetallicRoughnessMap) {
                float4 metallicRoughness = gMetallicRoughnessMap.Sample(gSampler, uv);
                roughness *= metallicRoughness.g;
                metallic *= metallicRoughness.b;
            }
            if (gMaterial.metallicOverride >= 0.0f) {
                metallic = gMaterial.metallicOverride;
            }
            if (gMaterial.roughnessOverride >= 0.0f) {
                roughness = gMaterial.roughnessOverride;
            }
            metallic = saturate(metallic);
            roughness = clamp(roughness, kMinRoughness, 1.0f);
            float3 toCamera = normalize(gCamera.worldPosition - input.worldPosition);
            float3 lightingResult = float3(0.0f, 0.0f, 0.0f);

            // ディレクショナルライト（影あり）
            {
                float3 toLight = -gDirectionLight.direction;
                float shadowFactor = CalculateDirectionalShadow(input.worldPosition, normal, dot(normal, toLight));
                lightingResult += PhysicalLight(normal, toCamera, toLight, gDirectionLight.color.rgb * gDirectionLight.intensity * shadowFactor,
                                                baseColor, metallic, roughness);
            }
            // 点光源
            {
                float3 toLight = gPointLight.position - input.worldPosition;
                float distance = length(toLight);
                float attenuation = pow(saturate(-distance / gPointLight.radius + 1.0f), gPointLight.decay);
                lightingResult += PhysicalLight(normal, toCamera, toLight / distance, gPointLight.color.rgb * gPointLight.intensity * attenuation,
                                                baseColor, metallic, roughness);
            }
            // スポットライト
            {
                float3 toLight = gSpotLight.position - input.worldPosition;
                float distance = length(toLight);
                float3 lightDirection = toLight / distance;
                float attenuation = pow(saturate(-distance / gSpotLight.distance + 1.0f), gSpotLight.decay);
                float cosAngle = dot(-lightDirection, gSpotLight.direction);
                float falloffFactor = saturate((cosAngle - gSpotLight.cosAngle) / (gSpotLight.cosFalloffStart - gSpotLight.cosAngle));
                lightingResult += PhysicalLight(normal, toCamera, lightDirection,
                                                gSpotLight.color.rgb * gSpotLight.intensity * attenuation * falloffFactor, baseColor, metallic, roughness);
            }

            // 周りの光（散らばる光と、景色の照り返しの代わり）
            float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
            float3 ambientDiffuse = baseColor * (1.0f - metallic) * kAmbientIntensity;
            float3 ambientSpecular = EnvironmentBRDF(f0, roughness, max(dot(normal, toCamera), 1e-4f)) * kAmbientSpecularIntensity;
            output.color.rgb = lightingResult + (ambientDiffuse + ambientSpecular) * occlusion;
        } else {
            // --- 今までの簡単な当たり方（光の向きだけで明るさが決まる。照り返しなし） ---
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

            float3 ambient = baseColor * kAmbientIntensity * occlusion;
            output.color.rgb = baseColor * lightingResult + ambient;
        }
    }
    output.color.rgb += emissive;

    return output;
}