// Blend all theme layers before the sole 8-bit output conversion. A fixed,
// zero-mean spatial dither hides quantization contours without animation.
cbuffer Parameters : register(b0) {
    float4 Base;
    float4 Region;
    float4 Pixels; // physical size xy, window-relative physical origin zw
    float4 Options; // dither amplitude in 8-bit channel levels
    float4 Colors[3];
    float4 Geometry[3]; // line start + projection, or circle center + radius
    float4 Fades[3]; // start, end, radial, enabled
};

struct Vertex {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Vertex VertexMain(uint id : SV_VertexID) {
    Vertex result;
    result.uv = float2((id << 1) & 2, id & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}

float4 GradientMain(Vertex input) : SV_Target {
    float2 position = Region.xy + input.uv * Region.zw;
    float3 color = Base.rgb;
    [unroll] for (int i = 0; i < 3; ++i) {
        if (Fades[i].w > 0) {
            float2 delta = position - Geometry[i].xy;
            float at = Fades[i].z > 0
                ? length(delta) / Geometry[i].z
                : dot(delta, Geometry[i].zw);
            float opacity = 1 - saturate((at - Fades[i].x) /
                                         (Fades[i].y - Fades[i].x));
            color = lerp(color, Colors[i].rgb, opacity);
        }
    }
    // Interleaved gradient noise: its coefficients decorrelate adjacent
    // physical pixels. No time input, so the background remains stationary.
    float2 pixel = floor(input.uv * Pixels.xy + Pixels.zw);
    float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    color += (noise - 0.5) * Options.x / 255;
    return float4(saturate(color), 1);
}
