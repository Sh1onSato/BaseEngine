struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// texelSize: 1テクセル分のUVオフセット, direction: 0.0=水平/1.0=垂直, blurStrength: ボケ幅の倍率
cbuffer BlurParam : register(b0) {
    float2 gTexelSize;
    float gDirection;
    float gBlurStrength;
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

// 5タップガウス重み（中心 + 左右対称2タップずつ）
static const float kWeights[5] = {
    0.227027f, 0.1945946f, 0.1216216f, 0.054054f, 0.016216f
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;

    float2 offsetDir = (gDirection < 0.5f) ? float2(gTexelSize.x, 0.0f) : float2(0.0f, gTexelSize.y);
    offsetDir *= gBlurStrength;

    float3 result = gTexture.Sample(gSampler, input.texcoord).rgb * kWeights[0];
    for (int i = 1; i < 5; ++i) {
        float2 offset = offsetDir * float(i);
        result += gTexture.Sample(gSampler, input.texcoord + offset).rgb * kWeights[i];
        result += gTexture.Sample(gSampler, input.texcoord - offset).rgb * kWeights[i];
    }

    output.color = float4(result, 1.0f);
    return output;
}
