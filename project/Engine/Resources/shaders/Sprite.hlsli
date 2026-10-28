// スプライト専用シェーダー共通定義（Sprite.VS.hlsl / Sprite.PS.hlsl）

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};
