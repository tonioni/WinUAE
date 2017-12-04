
#include "PixelShader.hlsl"

float4 PS_PostMask(PixelInputType input) : SV_TARGET
{
	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);
	float4 maskColor = maskTexture.Sample(SampleTypeWrap, input.sl);
	return textureColor * maskColor;
}
