struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};

TextureCube<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    // 3Dの方向ベクトルでキューブマップをサンプリング
    output.color = gTexture.Sample(gSampler, input.texcoord);
    return output;
}
