Texture2D<float4> src : register(t0);
RWTexture2D<float4> dest : register(u0);

cbuffer data : register(b0)
{
	float white_level;
	float reserved[3];
}

float3 soft_clip(float3 x)
{
	return saturate((1.0 + x - sqrt(1.0 - 1.99 * x + x * x)) / (1.995));
}

float3 linear_tonemap(float3 x)
{
	const float z = 0.8;
	const float d = 2.5;

	return lerp(x, (x - z) / d + z, step(z, x));
}

float3 bt2020_inv_gamma(float3 x)
{
	return (x > 0.00313066844250063) ? 1.055 * pow(clamp(x, 0, 10000), 1.0 / 2.4) - 0.055 : 12.92 * x;
}

float rgb_to_luma(float3 x)
{
	return dot(float3(0.213, 0.715, 0.072), x);
}

// Khronos PBR Neutral Tone Mapper
// https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral

// Input color is non-negative and resides in the Linear Rec. 709 color space.
// Output color is also Linear Rec. 709, but in the [0, 1] range.
float3 neutral(float3 color)
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;

	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression)
		return color;

	const float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	color *= newPeak / peak;

	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return lerp(color, newPeak.xxx, g);
}

[numthreads(16, 16, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	src.GetDimensions(width, height);

	if (tid.x >= width || tid.y >= height)
	{
		return;
	}

	float3 input_color = clamp(src[tid.xy].rgb, 0, 10000) / (white_level / 80);
	float3 linear_color = bt2020_inv_gamma(input_color);

	float3 linear_result = linear_tonemap(linear_color);
	float3 neutral_result = neutral(linear_color);

	float linear_luma = rgb_to_luma(linear_result);
	float neutral_luma = rgb_to_luma(neutral_result);
	float3 neutral_color = neutral_result / neutral_luma;

	linear_color = neutral_color * linear_luma;

	dest[tid.xy] = float4(linear_color, 1.0);
}
