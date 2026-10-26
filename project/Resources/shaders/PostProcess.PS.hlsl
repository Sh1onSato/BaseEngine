// PostProcess の最後の段: 3D の絵にブルームを足し、色を整え、ヴィネットを掛けて画面（黒帯の内側）に写す
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gScene : register(t0); // 3D の絵（画面全体のぼかしを掛けたときはぼかした絵）
Texture2D<float4> gBloom : register(t1); // ぼかした明るい所
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

// PostProcess::Render の constants と同じ並び
cbuffer CompositeParams : register(b0) {
    float gBloomIntensity;
    float gBrightness;
    float gContrast;
    float gSaturation;
    float gSepia;
    float gVignetteIntensity;
    float gVignetteRadius;
    float gUsePointFilter; // 1 なら引き伸ばしをくっきり（RenderScaler の kPoint）
    float3 gTint;
    float gPadding;
    float3 gVignetteColor;
    float gAspect; // 絵の幅 / 高さ（ヴィネットを楕円でなく円にする）
};

float4 main(VertexShaderOutput input) : SV_TARGET {
    float3 color = (gUsePointFilter > 0.5f)
        ? gScene.Sample(gPointSampler, input.texcoord).rgb
        : gScene.Sample(gLinearSampler, input.texcoord).rgb;

    // ブルーム（光を足す）
    color += gBloom.Sample(gLinearSampler, input.texcoord).rgb * gBloomIntensity;

    // 色の調整: 明るさ → コントラスト（0.5 の灰色を中心に） → 彩度 → セピア → 色を掛ける
    color += gBrightness;
    color = (color - 0.5f) * gContrast + 0.5f;
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luminance.xxx, color, gSaturation);
    float3 sepiaColor = float3(
        dot(color, float3(0.393f, 0.769f, 0.189f)),
        dot(color, float3(0.349f, 0.686f, 0.168f)),
        dot(color, float3(0.272f, 0.534f, 0.131f)));
    color = lerp(color, sepiaColor, gSepia);
    color *= gTint;

    // ヴィネット: 中心からの距離（0 で中心、1 で角。縦横比を直して円にする）が半径より外側ほど gVignetteColor に寄せる
    if (gVignetteIntensity > 0.0f) {
        float2 centered = (input.texcoord - 0.5f) * float2(gAspect, 1.0f) * 2.0f;
        float distanceFromCenter = length(centered) / sqrt(gAspect * gAspect + 1.0f);
        float amount = smoothstep(gVignetteRadius, 1.0f, distanceFromCenter) * gVignetteIntensity;
        color = lerp(color, gVignetteColor, amount);
    }
    return float4(saturate(color), 1.0f);
}
