
#include "PixelShader.hlsl"

float4 PS_PostPlain(PixelInputType input) : SV_TARGET
{
	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);
	return textureColor;
}
