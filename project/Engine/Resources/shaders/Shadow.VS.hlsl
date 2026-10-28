// シャドウマップ生成用の深度専用頂点シェーダー
// Object3d.VS.hlslと同じ頂点レイアウト（POSITION/TEXCOORD/NORMAL/BLENDWEIGHT0/BLENDINDICES0）を使い回すが、POSITIONのみ使用する。
// PSは存在しない（深度のみを書き込むパス）。
// スキニング処理もObject3d.VS.hlslとミラーリングすること（追従漏れがあると影だけバインドポーズのままになる）。

struct ShadowTransform {
    float4x4 wvp;
};
ConstantBuffer<ShadowTransform> gShadowTransform : register(b0);

// スキニング用ボーン行列パレット（非スキニングオブジェクトは単位行列1個をバインドする運用）
StructuredBuffer<float4x4> gSkinMatrices : register(t2);

struct VertexShaderInput {
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weights : BLENDWEIGHT0;
    uint4 indices : BLENDINDICES0;
};

float4 main(VertexShaderInput input) : SV_POSITION {
    float4x4 skinMatrix =
        gSkinMatrices[input.indices.x] * input.weights.x +
        gSkinMatrices[input.indices.y] * input.weights.y +
        gSkinMatrices[input.indices.z] * input.weights.z +
        gSkinMatrices[input.indices.w] * input.weights.w;
    float4 skinnedPosition = mul(input.position, skinMatrix);

    return mul(skinnedPosition, gShadowTransform.wvp);
}
