cbuffer AspectRatioBuffer : register(b0)
{
    float2 aspectRatio;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};


[shader("vertex")]
PSInput VSMain(float4 position: POSITION, float2 texcoord: TEXCOORD0)
{
    PSInput result;

    result.position = position * float4(aspectRatio.xy, 1.0, 1.0);
    result.texcoord = texcoord;

    return result;
}

[shader("pixel")]
float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.texcoord, 0.0, 1.0);
}
