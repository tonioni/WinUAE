
#include "PixelShader_HDR.hlsl"

float4 PS_PostPlain_HDR(PixelInputType input) : SV_TARGET
{
	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);

	return ConvertToHDR(textureColor);
}
