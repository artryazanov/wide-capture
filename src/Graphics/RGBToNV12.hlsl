Texture2D g_Input : register(t0);
SamplerState g_Sampler : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

// BT.709
static const float3 RGB2Y  = float3(0.2126, 0.7152, 0.0722);
static const float3 RGB2U  = float3(-0.1146, -0.3854, 0.5000);
static const float3 RGB2V  = float3(0.5000, -0.4542, -0.0458);

// Bufferless Quad VS
PS_INPUT VS(uint id : SV_VertexID) {
    PS_INPUT output;
    output.Tex = float2((id << 1) & 2, id & 2);
    output.Pos = float4(output.Tex * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float4 PS_Y(PS_INPUT input) : SV_Target {
    float3 rgb = g_Input.Sample(g_Sampler, input.Tex).rgb;
    float y = dot(rgb, RGB2Y);
    // Limited range: 16/255 + y * (219/255)? 
    // Usually standard definition is: Y = 16 + 219*Y_float.
    // But UNORM RTV expects [0..1].
    // If we want standard broadcast range (Limited):
    // return (16.0/255.0) + y * (219.0/255.0);
    // For Full range: return y;
    // Let's use Full Range for now unless colors look washed out.
    return y;
}

float2 PS_UV(PS_INPUT input) : SV_Target {
    float3 rgb = g_Input.Sample(g_Sampler, input.Tex).rgb;
    float u = dot(rgb, RGB2U) + 0.5;
    float v = dot(rgb, RGB2V) + 0.5;
    return float2(u, v);
}
