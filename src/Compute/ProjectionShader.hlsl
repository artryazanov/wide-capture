// ProjectionShader.hlsl
// Converts a Cubemap (or Texture2DArray of 6 faces) to Equirectangular Projection

TextureCube<float4> g_InputCubemap : register(t0);
RWTexture2D<float4> g_OutputTexture : register(u0);

SamplerState g_Sampler : register(s0);

static const float PI = 3.14159265359f;

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint width, height;
    g_OutputTexture.GetDimensions(width, height);

    if (DTid.x >= width || DTid.y >= height) return;

    // Normalizing coordinates to [0, 1]
    float2 uv = float2(DTid.x, DTid.y) / float2(width, height);

    // Spherical coordinates
    // Theta (Longitude): [-PI, PI]
    // Phi (Latitude): [-PI/2, PI/2]
    float theta = uv.x * 2.0f * PI - PI;
    float phi = uv.y * PI - PI / 2.0f;

    // Converting to direction vector
    // Assuming Y-up coordinate system
    float3 dir;
    dir.x = cos(phi) * sin(theta);
    dir.y = sin(phi);      
    dir.z = cos(phi) * cos(theta);

    // Sampling the cubemap
    // We sample LoD 0 directly
    float4 color = g_InputCubemap.SampleLevel(g_Sampler, normalize(dir), 0);

    g_OutputTexture[DTid.xy] = color;
}
