
Texture2D shaderTexture;
Texture2D maskTexture;
SamplerState SampleTypeClamp;
SamplerState SampleTypeWrap;
struct PixelInputType
{
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;
	float2 sl : TEXCOORD1;
};
