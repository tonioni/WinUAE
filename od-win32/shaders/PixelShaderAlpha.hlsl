
#include "PixelShader.hlsl"

float4 PS_PostAlpha(PixelInputType input) : SV_TARGET
{
	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);
	float4 maskColor = maskTexture.Sample(SampleTypeWrap, input.sl);
	return textureColor * (1 - maskColor.a) + (maskColor * maskColor.a);
}
