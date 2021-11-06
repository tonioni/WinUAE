
#include "PixelShader_HDR.hlsl"

float4 PS_PostAlpha_HDR(PixelInputType input) : SV_TARGET
{
	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);
	textureColor = ConvertToHDR(textureColor);
	float4 maskColor = maskTexture.Sample(SampleTypeWrap, input.sl);
	maskColor = ConvertToHDR(maskColor);
	return textureColor * (1 - maskColor.a) + (maskColor * maskColor.a);
}
