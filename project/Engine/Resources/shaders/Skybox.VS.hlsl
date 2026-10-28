struct TransformationMatrix {
    float4x4 WVP;
    float4x4 World;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput {
    float4 position : POSITION;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    // ローカル座標をそのまま方向ベクトルとして使用
    output.texcoord = input.position.xyz;
    // WVP行列を適用
    output.position = mul(input.position, gTransformationMatrix.WVP);
    // z値をw値と同じにすることで、射影空間でz/w = 1.0 (最背面) に固定する
    output.position.z = output.position.w;
    return output;
}
