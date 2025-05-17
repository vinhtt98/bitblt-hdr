Texture2D<float4> src : register(t0);
RWTexture2D<float4> dest : register(u0);

float3 soft_clip(float3 x)
{
	return saturate((1.0 + x - sqrt(1.0 - 1.99 * x + x * x)) / (1.995));
}

float3 aces_tonemap(float3 color)
{
    static const float3x3 m1 = {
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777
    };

    static const float3x3 m2 = {
        1.60475, -0.10208, -0.00327,
        -0.53108, 1.10813, -0.07276,
        -0.07367, -0.00605, 1.07602
    };

    float3 v = mul(color, m1);

    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;

    return mul(a / b, m2);
}

[numthreads(16, 16, 1)]
void main(uint3 tid: SV_DispatchThreadID)
{
    uint width, height;
    src.GetDimensions(width, height);

    if (tid.x >= width || tid.y >= height)
    {
        return;
    }

    float3 input_color = clamp(src[tid.xy].rgb, 0, 10000);

    float3 linear_color = pow(input_color, 0.4545);
    linear_color = linear_color * 2;
    linear_color = aces_tonemap(linear_color);
    linear_color = soft_clip(linear_color);

    float3 out_color = pow(linear_color, 2.2);

    dest[tid.xy] = float4(out_color, 1.0);
}
