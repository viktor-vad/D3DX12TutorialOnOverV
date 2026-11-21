struct AspectRatio
{
    float2 aspectRatio;
};

ConstantBuffer<AspectRatio> aspectRatioBuffer : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> Texture : register(t0);
SamplerState Sampler : register(s0);

[shader("vertex")]
PSInput VSMain(float4 position: POSITION, float2 texcoord: TEXCOORD0)
{
    PSInput result;

    result.position = position * float4(aspectRatioBuffer.aspectRatio.xy, 1.0, 1.0);
    result.texcoord = texcoord;

    return result;
}

[shader("pixel")]
float4 PSMain(PSInput input) : SV_TARGET
{
    return Texture.Sample( Sampler, input.texcoord );
}
