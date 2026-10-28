#include "Sprite.hlsli"

// C++側のSpriteMaterialと同じレイアウト
struct SpriteMaterial {
    float4 color;
    float4x4 uvTransform;
    int usePointFilter; // 0: リニア補間 / 0以外: ポイント(ニアレスト)
    float3 padding;
};
ConstantBuffer<SpriteMaterial> gMaterial : register(b0);

Texture2D<float4> gTexture : register(t0);
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor;
    if (gMaterial.usePointFilter != 0) {
        textureColor = gTexture.Sample(gPointSampler, transformedUV.xy);
    } else {
        textureColor = gTexture.Sample(gLinearSampler, transformedUV.xy);
    }
    // 半透明を活かすためdiscardはしない（アルファはブレンドステートに任せる）
    output.color = gMaterial.color * textureColor;
    return output;
}
