#include "object3d.hlsli"

struct TransformationMatrix{
    float4x4 WVP;
    float4x4 World;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b1);

// スキニング用ボーン行列パレット（非スキニングオブジェクトは単位行列1個をバインドする運用）
StructuredBuffer<float4x4> gSkinMatrices : register(t2);

struct VertexShaderInput{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weights : BLENDWEIGHT0;
    uint4 indices : BLENDINDICES0;
};

VertexShaderOutput main(VertexShaderInput input){
    VertexShaderOutput output;

    // 4ボーン分のブレンド行列を計算してからWVP/World変換に渡す
    float4x4 skinMatrix =
        gSkinMatrices[input.indices.x] * input.weights.x +
        gSkinMatrices[input.indices.y] * input.weights.y +
        gSkinMatrices[input.indices.z] * input.weights.z +
        gSkinMatrices[input.indices.w] * input.weights.w;
    float4 skinnedPosition = mul(input.position, skinMatrix);
    float3 skinnedNormal = mul(float4(input.normal, 0.0f), skinMatrix).xyz;

    output.position = mul(skinnedPosition, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(skinnedNormal, 0.0f), gTransformationMatrix.World).xyz);
    output.worldPosition = mul(skinnedPosition, gTransformationMatrix.World).xyz;
    return output;
}