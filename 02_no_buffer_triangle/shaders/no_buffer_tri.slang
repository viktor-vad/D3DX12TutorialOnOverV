
struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

static float4 positions[3] = {
    float4(0.0f, 0.5f, 0.0f,1.0),
    float4(0.5f, -0.5f, 0.0f,1.0),
    float4(-0.5f, -0.5f, 0.0f,1.0)
};

static float4 colors[3] = {
    float4(1.0f, 0.0f, 0.0f, 1.0),
    float4(0.0f, 1.0f, 0.0f, 1.0),
    float4(0.0f, 0.0f, 1.0f, 1.0)
};

[shader("vertex")]
PSInput VSMain(uint vertexID : SV_VertexID)
{
    PSInput result;

    result.position = positions[vertexID];
    result.color = colors[vertexID];

    return result;
}

[shader("pixel")]
float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
