// 画面全体を覆う大きな三角形1枚（頂点バッファを使わず、頂点番号から位置を作る）。
// RenderScaler が、小さい解像度で描いた 3D の絵を画面に引き伸ばすときに使う
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(uint vertexId : SV_VertexID) {
    // 0: 左上(0,0) 1: 右上の外(2,0) 2: 左下の外(0,2) … 画面を全部覆う
    float2 texcoord = float2((vertexId << 1) & 2, vertexId & 2);
    VertexShaderOutput output;
    output.position = float4(texcoord.x * 2.0f - 1.0f, 1.0f - texcoord.y * 2.0f, 0.0f, 1.0f);
    output.texcoord = texcoord;
    return output;
}
