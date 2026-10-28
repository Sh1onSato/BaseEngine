// デバッグ線（DebugDraw）。頂点はワールド座標（2D はゲーム座標）で、行列はルート定数で受け取る
cbuffer DebugLineConstants : register(b0) {
    float4x4 gViewProjection;
};

struct VertexShaderInput {
    float4 position : POSITION0;
    float4 color : COLOR0;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.position = mul(input.position, gViewProjection);
    output.color = input.color;
    return output;
}
