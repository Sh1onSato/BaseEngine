#include "Sprite.hlsli"

// C++側のTransformationMatrix(WVP, World)の先頭WVPのみ参照する
struct SpriteTransform {
    float4x4 WVP;
};
ConstantBuffer<SpriteTransform> gTransform : register(b1);

struct VertexShaderInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.position = mul(input.position, gTransform.WVP);
    output.texcoord = input.texcoord;
    return output;
}
