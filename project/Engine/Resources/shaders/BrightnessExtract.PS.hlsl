// PostProcess のブルーム 1段目: しきい値より明るい所だけを取り出す（半分の解像度に縮めながら）
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer ExtractParams : register(b0) {
    float gThreshold; // 0〜1。これより明るい所が光る
};

float4 main(VertexShaderOutput input) : SV_TARGET {
    float3 color = gTexture.Sample(gLinearSampler, input.texcoord).rgb;
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    // しきい値でぱっと切ると光の縁がちらつくので、しきい値から 1 にかけてなめらかに強くする
    float weight = saturate((luminance - gThreshold) / max(1.0f - gThreshold, 1.0e-4f));
    return float4(color * weight, 1.0f);
}
