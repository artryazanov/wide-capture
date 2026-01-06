Texture2D<float4> InputTexture : register(t0);
RWTexture2D<unorm float> OutputY : register(u0);
RWTexture2D<unorm float2> OutputUV : register(u1);

// BT.709 coefficients
static const float3 RGB2Y  = float3(0.2126, 0.7152, 0.0722);
static const float3 RGB2U  = float3(-0.1146, -0.3854, 0.5000);
static const float3 RGB2V  = float3(0.5000, -0.4542, -0.0458);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadId.xy;
    uint width, height;
    InputTexture.GetDimensions(width, height);

    if (pos.x >= width || pos.y >= height) return;

    // Read RGB (2x2 block logic handled by thread ID mapping?)
    // NV12 has full res Y, half res UV.
    // We can compute Y for every pixel.
    // For UV, we compute only on even coordinates (2x2 block top-left).
    
    // Sample directly texelFetch
    float4 pixel = InputTexture[pos];
    // Gamma correction? Assuming Input is sRGB (UNORM), shader reads linear? 
    // Usually R8G8B8A8_UNORM SRV reads as linearized float. 
    // NVENC expects YUV usually in limited range or full range. 
    // We'll output YUV.

    float y = dot(pixel.rgb, RGB2Y);
    OutputY[pos] = y;

    // Subsample UV
    if ((pos.x % 2 == 0) && (pos.y % 2 == 0)) {
        // Average 4 pixels for better quality? 
        // Or simple subsample (top-left). Let's do simple for speed first.
        
        // Better: Average.
        // float4 p00 = InputTexture[pos]; // Already fetched
        // float4 p01 = InputTexture[uint2(pos.x+1, pos.y)];
        // float4 p10 = InputTexture[uint2(pos.x, pos.y+1)];
        // float4 p11 = InputTexture[uint2(pos.x+1, pos.y+1)];
        // float3 avgRGB = (p00.rgb + p01.rgb + p10.rgb + p11.rgb) * 0.25;
        
        // But checking bounds:
        float3 avgRGB = pixel.rgb; // Fallback
        if (pos.x + 1 < width && pos.y + 1 < height) {
             float3 p01 = InputTexture[uint2(pos.x+1, pos.y)].rgb;
             float3 p10 = InputTexture[uint2(pos.x, pos.y+1)].rgb;
             float3 p11 = InputTexture[uint2(pos.x+1, pos.y+1)].rgb;
             avgRGB = (pixel.rgb + p01 + p10 + p11) * 0.25;
        }

        float u = dot(avgRGB, RGB2U) + 0.5;
        float v = dot(avgRGB, RGB2V) + 0.5;
        
        OutputUV[uint2(pos.x / 2, pos.y / 2)] = float2(u, v);
    }
}
