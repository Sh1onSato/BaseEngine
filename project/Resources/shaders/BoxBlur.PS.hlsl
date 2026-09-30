struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// texelSize: 1テクセル分のUVオフセット, direction: 0.0=水平/1.0=垂直, blurStrength: ボケ幅の倍率
// GaussianBlur.PS.hlslと同じCBuffer構成・タップ数にしてあり、PostProcess側は同じ描画関数から
// PSOだけ切り替えて呼び出せる（ルートシグネチャ共用）
cbuffer BlurParam : register(b0) {
    float2 gTexelSize;
    float gDirection;
    float gBlurStrength;
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

// ボックスフィルター：ガウシアン（中心ほど重みが大きい）と違い、全タップを均等重みで平均する
static const int kTapCount = 9; // 中心 + 左右対称4タップずつ

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;

    float2 offsetDir = (gDirection < 0.5f) ? float2(gTexelSize.x, 0.0f) : float2(0.0f, gTexelSize.y);
    offsetDir *= gBlurStrength;

    float3 result = gTexture.Sample(gSampler, input.texcoord).rgb;
    for (int i = 1; i <= 4; ++i) {
        float2 offset = offsetDir * float(i);
        result += gTexture.Sample(gSampler, input.texcoord + offset).rgb;
        result += gTexture.Sample(gSampler, input.texcoord - offset).rgb;
    }

    output.color = float4(result / float(kTapCount), 1.0f);
    return output;
}
