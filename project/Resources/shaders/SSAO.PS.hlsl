// 画面の効果の1段: 周りの物に囲まれて光が届きにくい所（物が接している所・すき間・角）を暗くする（SSAO）。
// 深度だけから、その点の位置と面の向きを作り、面の外側の半球の中の何か所かが「何かの奥」に入っているかを数える
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float> gDepth : register(t0); // 3D を描いたときの深度（0〜1）
SamplerState gPointSampler : register(s1);

// PostProcess::Render の AO の constants と同じ並び
cbuffer AoParams : register(b0) {
    // 投影行列の成分（深度から位置に戻す・位置から画面に写す）
    float gProjection00;
    float gProjection11;
    float gProjection22;
    float gProjection32;
    float gRadius;    // 調べる範囲（ワールドの長さ）
    float gIntensity; // 暗くする強さ
    float gBias;      // これより近い差は「奥にある」とみなさない（平らな面が自分で暗くならないように）
    float gPadding;
    float2 gDepthTexel; // 深度の 1 ピクセル（UV）
};

static const int kSampleCount = 16;
// これより奥（深度 1 に近い）は何も描いていない（背景）
static const float kBackgroundDepth = 0.9999f;

// UV の所のカメラから見た位置（x 右・y 上・z 奥）
float3 ViewPosition(float2 uv) {
    float depth = gDepth.SampleLevel(gPointSampler, uv, 0.0f);
    float z = gProjection32 / (depth - gProjection22);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    return float3(ndc.x * z / gProjection00, ndc.y * z / gProjection11, z);
}

// 画面の位置ごとに違う 0〜1 の値（毎ピクセル違う向きに調べて、少ない数でもむらを細かくする。後でぼかして消す）
float InterleavedGradientNoise(float2 pixel) {
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

float4 main(VertexShaderOutput input) : SV_TARGET {
    float2 uv = input.texcoord;
    float depth = gDepth.SampleLevel(gPointSampler, uv, 0.0f);
    if (depth >= kBackgroundDepth) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    float3 position = ViewPosition(uv);

    // 面の向き: 左右・上下の近い方の点との差から作る（物の縁で向こう側の物と混ざらないように）
    float3 right = ViewPosition(uv + float2(gDepthTexel.x, 0.0f)) - position;
    float3 left = position - ViewPosition(uv - float2(gDepthTexel.x, 0.0f));
    float3 down = ViewPosition(uv + float2(0.0f, gDepthTexel.y)) - position;
    float3 up = position - ViewPosition(uv - float2(0.0f, gDepthTexel.y));
    float3 horizontal = abs(right.z) < abs(left.z) ? right : left;
    float3 vertical = abs(down.z) < abs(up.z) ? down : up;
    float3 normal = normalize(cross(horizontal, vertical));
    if (dot(normal, position) > 0.0f) {
        normal = -normal; // カメラの方を向かせる
    }

    // 面に沿った向きを、ピクセルごとに違う角度で作る
    float angle = InterleavedGradientNoise(input.position.xy) * 6.2831853f;
    float3 randomVector = float3(cos(angle), sin(angle), 0.0f);
    float3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
    float3 bitangent = cross(normal, tangent);

    float occlusion = 0.0f;
    [unroll]
    for (int i = 0; i < kSampleCount; ++i) {
        // 半球の中に、中心に近いほど多く散らばる点（らせん状に並べる）
        float t = (i + 0.5f) / kSampleCount;
        float cosTheta = sqrt(1.0f - t);
        float sinTheta = sqrt(t);
        float phi = i * 2.39996323f; // 黄金角
        float3 direction = tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + normal * cosTheta;
        float scale = lerp(0.1f, 1.0f, t * t);
        float3 samplePosition = position + direction * (gRadius * scale);

        // その点を画面に写して、そこに描かれている物の奥行きと比べる
        float2 sampleUV = float2(samplePosition.x * gProjection00 / samplePosition.z * 0.5f + 0.5f,
                                 0.5f - samplePosition.y * gProjection11 / samplePosition.z * 0.5f);
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) {
            continue;
        }
        float sceneZ = ViewPosition(sampleUV).z;
        // 手前に物があれば遮られている。ただし調べる範囲より大きく手前の物（遠くの別の物）は数えない
        float isOccluded = sceneZ <= samplePosition.z - gBias ? 1.0f : 0.0f;
        float rangeCheck = smoothstep(0.0f, 1.0f, gRadius / max(abs(position.z - sceneZ), 1e-4f));
        occlusion += isOccluded * rangeCheck;
    }
    // 床と壁の角のように「半分が遮られている」所でしっかり暗くなるよう、遮られた割合を 2 倍にする
    float ambientOcclusion = saturate(1.0f - occlusion / kSampleCount * 2.0f * gIntensity);
    return float4(ambientOcclusion, ambientOcclusion, ambientOcclusion, 1.0f);
}
