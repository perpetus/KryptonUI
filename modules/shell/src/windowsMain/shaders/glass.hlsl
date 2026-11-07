// Kube Searchbox filter, adapted to public D3D/Composition. See docs/liquid-glass.md.
// Only Quality surfaces that float over the page run these passes.
Texture2D Background : register(t0);
Texture2D Curve : register(t1);
SamplerState LinearClamp : register(s0);
cbuffer Parameters : register(b0) {
    float4 Shape;       // client bounds, DIPs
    float4 Geometry;    // radius, bezel, DPI, optical profile row
    float4 Optics;      // reserved, specular opacity, specular width, specular saturation
    float4 Light;       // direction xy
    float4 Tint;        // straight-alpha overlay
    float4 Base;        // opaque color under transparent backdrop pixels
    float4 Source;      // live background bounds, DIPs
    float4 Work;        // texel size xy; blur direction or refraction scale/map maximum zw
    float4 Options;     // reserved, blur sigma in texels
};
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VertexMain(uint id : SV_VertexID) {
    Vertex result;
    result.uv = float2((id << 1) & 2, id & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}
float3 Transmission(float2 samplePosition) {
    float2 uv = (samplePosition - Source.xy) / max(Source.zw, 0.001);
    float4 c = Background.SampleLevel(LinearClamp, clamp(uv, Work.xy * 0.5, 1 - Work.xy * 0.5), 0);
    // The copy is premultiplied; an unpainted host frame is transparent.
    return c.rgb + Base.rgb * (1 - c.a);
}
float4 GlassMain(Vertex input) : SV_Target {
    float2 p = (input.uv - 0.5) * Shape.zw;
    float radius = min(Geometry.x, min(Shape.z, Shape.w) * 0.5);
    float2 q = abs(p) - (Shape.zw * 0.5 - radius);
    float2 outside = max(q, 0);
    float distance = length(outside) + min(max(q.x, q.y), 0) - radius;
    float coverage = saturate(0.5 - distance * Geometry.z);
    if (coverage <= 0) return 0;
    float2 normal = dot(outside, outside) > 0.00001 ? normalize(outside)
                    : q.x > q.y ? float2(1, 0) : float2(0, 1);
    normal *= float2(p.x < 0 ? -1 : 1, p.y < 0 ? -1 : 1);
    float inset = max(0, -distance);
    float t = inset / max(min(Geometry.y, radius - 1), 0.001);
    float index = min(255, floor(saturate(t) * 256));
    float displacement = t < 1
        ? Curve.SampleLevel(LinearClamp, float2((index + 0.5) / 256, Geometry.w), 0).r
        : 0;
    // SVG feDisplacementMap reads RG8 around 0.5. Preserve its normalization,
    // quantization and half-scale; applying the raw Snell distance doubles it.
    float2 map = round(clamp(128 - normal * displacement / max(Work.w, 0.001) * 127, 0, 255));
    float2 offset = (map / 255 - 0.5) * Work.w * Work.z;
    float2 samplePosition = Shape.xy + input.uv * Shape.zw + offset;
    float3 color = Transmission(samplePosition);
    // Kube's specular map has a two-DIP support, peaking one DIP inside.
    // Saturation affects only this mask, leaving the center clear.
    float band = inset / max(Optics.z, 0.001);
    float coefficient = abs(dot(normal * float2(1, -1), Light.xy)) *
                        sqrt(max(0, 1 - (1 - band) * (1 - band)));
    float specularAlpha = coefficient * coefficient;
    float luminance = dot(color, float3(0.213, 0.715, 0.072));
    float3 saturated = saturate(lerp(luminance.xxx, color, Optics.w));
    color = lerp(color, saturated, specularAlpha);
    color = lerp(color, coefficient.xxx, specularAlpha * Optics.y);
    color = lerp(color, Tint.rgb, Tint.a);
    return float4(saturate(color) * coverage, coverage);
}
// Half-resolution 2x2 box: Work.xy is the source texel size.
float4 DownsampleMain(Vertex input) : SV_Target {
    float2 d = Work.xy * 0.5;
    return (Background.SampleLevel(LinearClamp, input.uv + float2(-d.x, -d.y), 0) +
            Background.SampleLevel(LinearClamp, input.uv + float2(d.x, -d.y), 0) +
            Background.SampleLevel(LinearClamp, input.uv + float2(-d.x, d.y), 0) +
            Background.SampleLevel(LinearClamp, input.uv + d, 0)) * 0.25;
}
float4 BlurMain(Vertex input) : SV_Target {
    float sigma = max(Options.y, 0.001);
    int radius = min(12, (int)ceil(3 * sigma));
    float4 color = 0;
    float total = 0;
    [loop] for (int i = -radius; i <= radius; ++i) {
        float weight = exp(-0.5 * i * i / (sigma * sigma));
        color += Background.SampleLevel(LinearClamp, input.uv + Work.xy * Work.zw * i, 0) * weight;
        total += weight;
    }
    // Blur premultiplied channels together to avoid dark alpha fringes.
    return color / max(total, 0.001);
}
