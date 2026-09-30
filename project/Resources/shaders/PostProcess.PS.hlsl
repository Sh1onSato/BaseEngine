struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gBloomTexture : register(t1);
Texture2D<float4> gBlurTexture : register(t2);
SamplerState gSampler : register(s0);

// ブルームをどれだけ加算合成するかの強度／グレースケール化・セピア化・画面全体ぼかし・ヴィネットをするかどうか
cbuffer CompositeParam : register(b0) {
    float gBloomIntensity;
    float gGrayscale;         // 0=通常表示, 1=グレースケール表示
    float gSepia;             // 0=通常表示, 1=セピア表示（gGrayscaleより優先）
    float gBlurEnabled;       // 0=通常表示, 1=画面全体ぼかし表示（ブルームとは独立して重ねがけされる）
    float gVignetteIntensity;       // 0=効果なし、1で画面端が真っ黒になるまで減光
    float gVignetteRadius;          // ここより中心側は減光しない範囲（0=中心から、1で端のみ）
    float gDamageVignetteIntensity; // 被弾フラッシュ用。0=効果なし、上のvignetteとは独立に赤くブレンドする
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;

    float4 sceneColor = gTexture.Sample(gSampler, input.texcoord);

    if (gBlurEnabled > 0.5f) {
        sceneColor = gBlurTexture.Sample(gSampler, input.texcoord);
    }

    float4 bloomColor = gBloomTexture.Sample(gSampler, input.texcoord);

    output.color.rgb = sceneColor.rgb + bloomColor.rgb * gBloomIntensity;

    if (gSepia > 0.5f) {
        // 回想シーン風のセピア調（定番の変換行列で赤み・黄み寄りの色に写像する）
        float3 sepiaColor;
        sepiaColor.r = dot(output.color.rgb, float3(0.393f, 0.769f, 0.189f));
        sepiaColor.g = dot(output.color.rgb, float3(0.349f, 0.686f, 0.168f));
        sepiaColor.b = dot(output.color.rgb, float3(0.272f, 0.534f, 0.131f));
        output.color.rgb = saturate(sepiaColor);
    } else {
        // 輝度に変換し、gGrayscaleの値に応じて通常色とブレンドする（1.0で完全グレースケール）
        float luminance = dot(output.color.rgb, float3(0.299f, 0.587f, 0.114f));
        output.color.rgb = lerp(output.color.rgb, luminance.xxx, gGrayscale);
    }

    if (gVignetteIntensity > 0.0f) {
        // 中心からの距離(0=中心, 1=画面の縦横の端)を求め、gVignetteRadiusより外側だけ滑らかに減光する
        float2 centered = input.texcoord - 0.5f;
        float dist = length(centered) * 2.0f;
        float vignette = smoothstep(gVignetteRadius, 1.0f, dist);
        output.color.rgb *= (1.0f - vignette * gVignetteIntensity);
    }

    if (gDamageVignetteIntensity > 0.0f) {
        // 被弾時の赤ヴィネットは上の黒ヴィネットとは別枠。半径は演出用に固定し、赤へ寄せる(乗算ではなくlerp)
        float2 centered = input.texcoord - 0.5f;
        float dist = length(centered) * 2.0f;
        float damageVignette = smoothstep(0.35f, 1.0f, dist) * gDamageVignetteIntensity;
        output.color.rgb = lerp(output.color.rgb, float3(0.6f, 0.0f, 0.0f), damageVignette);
    }

    output.color.a = sceneColor.a;

    return output;
}
