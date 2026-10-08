cbuffer Scene : register(b0) {
    row_major float4x4 worldViewProjection;
    float4 objectColor;
    float4 materialFlags;
};
Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);
struct VIn { float3 position:POSITION; float2 uv:TEXCOORD0; };
struct VOut { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
VOut VSMain(VIn input) {
    VOut output;
    output.position=mul(float4(input.position,1),worldViewProjection);
    output.uv=input.uv;
    return output;
}
float4 PSMain(VOut input):SV_TARGET {
    float4 base=objectColor;
    if(materialFlags.x>0.5f)base*=diffuseTexture.Sample(diffuseSampler,input.uv);
    clip(base.a - 0.025f);
    return base;
}
