struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct VertexShaderInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    // 座標変換を行わず、そのまま渡す（スプライト側で既に正規化デバイス座標系になっている前提）
    output.position = input.position;
    output.texcoord = input.texcoord;
    return output;
}
