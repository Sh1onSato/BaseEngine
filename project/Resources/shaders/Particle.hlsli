struct VertexShaderInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
};

struct ParticleForGPU {
    float4x4 WVP;
    float4x4 World;
    float4 color;
    float2 uvOffset; // スプライトシートのUVオフセット（現在のコマの左上）
    float2 uvScale;  // スプライトシートの1コマ分のUVサイズ
};
