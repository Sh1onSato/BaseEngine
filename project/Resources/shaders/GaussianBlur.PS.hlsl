// PostProcess のぼかし（横か縦の一方向）。横 → 縦の2回で、四角い範囲をぼかしたのと同じになる
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 gStep; // 1 回にずらす量（UV）。横なら (半径 / 幅, 0)、縦なら (0, 半径 / 高さ)
};

// 中心と左右4つずつの重み（合計 1）
static const float kWeights[5] = { 0.227027f, 0.1945946f, 0.1216216f, 0.054054f, 0.016216f };

float4 main(VertexShaderOutput input) : SV_TARGET {
    float3 result = gTexture.Sample(gLinearSampler, input.texcoord).rgb * kWeights[0];
    for (int i = 1; i < 5; ++i) {
        float2 offset = gStep * float(i);
        result += gTexture.Sample(gLinearSampler, input.texcoord + offset).rgb * kWeights[i];
        result += gTexture.Sample(gLinearSampler, input.texcoord - offset).rgb * kWeights[i];
    }
    return float4(result, 1.0f);
}
