struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// しきい値を超えた輝度成分だけを抽出するための定数
cbuffer ExtractParam : register(b0) {
    float gThreshold;
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;

    float4 sceneColor = gTexture.Sample(gSampler, input.texcoord);
    float luminance = dot(sceneColor.rgb, float3(0.2125f, 0.7154f, 0.0721f));

    if (luminance > gThreshold) {
        output.color = float4(sceneColor.rgb, 1.0f);
    } else {
        output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return output;
}
