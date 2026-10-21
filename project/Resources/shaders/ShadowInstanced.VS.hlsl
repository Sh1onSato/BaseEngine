// シャドウマップ生成用のインスタンシング頂点シェーダー（ModelInstancer::DrawShadow が使う）。
// Object3dInstanced.VS.hlsl と同じインスタンスデータ（t3）を読み、ライト視点で深度だけ書く。PS は無い。

// ライト視点のビュー × プロジェクション
struct LightViewProjection {
    float4x4 viewProjection;
};
ConstantBuffer<LightViewProjection> gLightViewProjection : register(b0);

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

float4 main(VertexShaderInput input, uint instanceId : SV_InstanceID) : SV_POSITION {
    float4 worldPosition = mul(input.position, gInstances[instanceId].world);
    return mul(worldPosition, gLightViewProjection.viewProjection);
}
