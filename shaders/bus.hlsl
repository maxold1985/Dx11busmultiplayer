cbuffer Scene : register(b0) { row_major float4x4 worldViewProjection; float4 objectColor; };
struct VIn { float3 position:POSITION; };
struct VOut { float4 position:SV_POSITION; };
VOut VSMain(VIn input) { VOut o; o.position=mul(float4(input.position,1),worldViewProjection);return o; }
float4 PSMain(VOut input):SV_TARGET {return objectColor;}
