// 小さい解像度で描いた 3D の絵を、画面の大きさに引き伸ばして写す
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct ScaleParams {
    uint usePointFilter; // 1 ならくっきり（ドット絵向け）、0 ならなめらか
};
ConstantBuffer<ScaleParams> gParams : register(b0);

Texture2D<float4> gTexture : register(t0);
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

float4 main(VertexShaderOutput input) : SV_TARGET {
    if (gParams.usePointFilter != 0) {
        return gTexture.Sample(gPointSampler, input.texcoord);
    }
    return gTexture.Sample(gLinearSampler, input.texcoord);
}
