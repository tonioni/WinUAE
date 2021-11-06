
Texture2D shaderTexture;
Texture2D maskTexture;
SamplerState SampleTypeClamp;
SamplerState SampleTypeWrap;

cbuffer PS_CONSTANT_BUFFER
{
	float brightness;
	float contrast;
	float d2;
	float d3;
};


struct PixelInputType
{
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;
	float2 sl : TEXCOORD1;
};

float4x4 brightnessMatrix()
{
	return float4x4(1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		brightness, brightness, brightness, 1);
}

float4x4 contrastMatrix()
{
	float t = (1.0 - contrast) / 2.0;

	return float4x4(contrast, 0, 0, 0,
		0, contrast, 0, 0,
		0, 0, contrast, 0,
		t, t, t, 1);

}

float3 RemoveSRGBCurve(float3 x)
{
	// Approximately pow(x, 2.2)
	return x < 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

float4 ConvertToHDR(float4 color)
{
	float4 c;

	c = color;
	if (c.a > 0 && c.a < 2.0/256.0)
		return float4(0, 0, 0, 0);

	c.rgb = (c.rgb - 0.5f) * contrast + 0.5f;
	c.rgb *= brightness;

	c.rgb = RemoveSRGBCurve(c.rgb);

	c.rgb *= 300.0f / 80.0f;
	c.rgb += 0.0001;

	c.a = color.a;

	return c;
}

