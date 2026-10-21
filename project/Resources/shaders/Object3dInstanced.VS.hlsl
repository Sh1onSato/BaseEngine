#include "object3d.hlsli"

// インスタンシング描画用の頂点シェーダー（ModelInstancer が使う）。
// 同じモデルを、インスタンスごとに違うワールド行列・色で1回の DrawInstanced でまとめて描く。
// PS は Object3d.PS.hlsl を共用する。スキニングはしない（BLENDWEIGHT / BLENDINDICES は読まない）。
// 影は ShadowInstanced.VS.hlsl が同じインスタンスデータ（t3）を読む。

// カメラのビュー × プロジェクション（ワールド行列はインスタンスごとに t3 から読む）
struct ViewProjection {
    float4x4 viewProjection;
};
ConstantBuffer<ViewProjection> gViewProjection : register(b1);

// C++ 側の ModelInstancer::InstanceData と同じ並びにすること
struct InstanceData {
    float4x4 world;
    float4 color;
};
StructuredBuffer<InstanceData> gInstances : register(t3);

struct VertexShaderInput {
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weights : BLENDWEIGHT0;
    uint4 indices : BLENDINDICES0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID) {
    InstanceData instance = gInstances[instanceId];
    float4 worldPosition = mul(input.position, instance.world);

    VertexShaderOutput output;
    output.position = mul(worldPosition, gViewProjection.viewProjection);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(input.normal, 0.0f), instance.world).xyz);
    output.worldPosition = worldPosition.xyz;
    output.instanceColor = instance.color;
    return output;
}
