struct VertexShaderOutput{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    // インスタンスごとの色（Object3dInstanced.VS が設定。通常の Object3d.VS は白）。PS で gMaterial.color に掛ける
    float4 instanceColor : COLOR0;
};
