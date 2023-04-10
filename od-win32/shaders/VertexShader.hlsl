
cbuffer MatrixBuffer
{
	matrix worldMatrix;
	matrix viewMatrix;
	matrix projectionMatrix;
};
struct VertexInputType
{
	float4 position : POSITION;
	float2 tex : TEXCOORD0;
	float2 sl : TEXCOORD1;
};
struct PixelInputType
{
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;
	float2 sl : TEXCOORD1;
};
PixelInputType TextureVertexShader(VertexInputType input)
{
	PixelInputType output;
	input.position.w = 1.0f;
	output.position = mul(input.position, worldMatrix);
	output.position = mul(output.position, viewMatrix);
	output.position = mul(output.position, projectionMatrix);
	output.position.z = 0.0f;
	output.tex = input.tex;
	output.sl = input.sl;
	return output;
}
