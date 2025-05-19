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

float3 aces_tonemap(float3 color)
{
	static const float3x3 m1 =
	{
		0.59719, 0.07600, 0.02840,
            0.35458, 0.90834, 0.13383,
            0.04823, 0.01566, 0.83777
	};

	static const float3x3 m2 =
	{
		1.60475, -0.10208, -0.00327,
            -0.53108, 1.10813, -0.07276,
            -0.07367, -0.00605, 1.07602
	};

	float3 v = mul(color, m1);

	float3 a = v * (v + 0.0245786) - 0.000090537;
	float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;

	return mul(a / b, m2);
}

////////////////////////////////////////////////////////////////////////////////
// Uchimura 2017, "HDR theory and practice"
// Math: https://www.desmos.com/calculator/gslcdxvipg
// Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
float3 uchimura(float3 x, float P, float a, float m, float l, float c, float b)
{
	float l0 = ((P - m) * l) / a;
	float L0 = m - m / a;
	float L1 = m + (1.0 - m) / a;
	float S0 = m + l0;
	float S1 = m + a * l0;
	float C2 = (a * P) / (P - S1);
	float CP = -C2 / P;

	float3 w0 = float3(1.0 - smoothstep(0.0, m, x));
	float3 w2 = float3(step(m + l0, x));
	float3 w1 = float3(1.0 - w0 - w2);

	float3 T = float3(m * pow(x / m, c) + b);
	float3 S = float3(P - (P - S1) * exp(CP * (x - S0)));
	float3 L = float3(m + a * (x - m));

	return T * w0 + L * w1 + S * w2;
}

float3 uchimura(float3 x)
{
	const float P = 1; // max display brightness
	const float a = 1; // contrast
	const float m = 0.22; // linear section start
	const float l = 0.4; // linear section length
	const float c = 1.33; // black
	const float b = 0.0; // pedestal

	return uchimura(x, P, a, m, l, c, b);
}

float3 linear_tonemap(float3 x)
{
	const float z = 0.8;
	const float d = 2;

	return lerp(x, (x - z) / d + z, step(z, x));
}

float3 bt2020_inv_gamma(float3 x)
{
	return (x > 0.00313066844250063) ? 1.055 * pow(x, 1.0 / 2.4) - 0.055 : 12.92 * x;
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
	const float gamma = 2.0;

	float3 linear_color = bt2020_inv_gamma(input_color);
	linear_color = linear_tonemap(linear_color);
	float3 out_color = linear_color;

	dest[tid.xy] = float4(out_color, 1.0);
}
