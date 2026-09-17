// ScaleBlit: bilinear-resampled copy used by the present path to map the
// working-extent frame onto the swapchain backbuffer (video extent may differ
// from window extent). SDR path only: straight RGBA8 in, RGBA8 out.

#include "HdrColor.hlsli"
Texture2D<float4> sourceTex : register(t0);
RWTexture2D<float4> outputTex : register(u0);

cbuffer ScaleBlitConstants : register(b0)
{
    uint sourceWidth;
    uint sourceHeight;
    uint outputWidth;
    uint outputHeight;
    float encodeSrgb; // 1 only at linear working -> SDR output boundary
    float ditherStep; // 1 LSB of the target (0 = no dither)
    float _pad2;
    float _pad3;
};

groupshared float4 tile[16][16];

[numthreads(16, 16, 1)]
void main(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID, uint3 globalId : SV_DispatchThreadID)
{
    if (globalId.x >= outputWidth || globalId.y >= outputHeight) {
        return;
    }
    float4 value;
    if(sourceWidth==outputWidth&&sourceHeight==outputHeight){
        // Avoid reciprocal/interpolation rounding on tall 1:1 images.
        value=sourceTex[globalId.xy];
    }else{
    // Map output pixel center back into source coordinates.
    const float scaleX = float(sourceWidth) / float(outputWidth);
    const float scaleY = float(sourceHeight) / float(outputHeight);
    const float sx = (float(globalId.x) + 0.5) * scaleX - 0.5;
    const float sy = (float(globalId.y) + 0.5) * scaleY - 0.5;
    const int x0 = int(floor(sx));
    const int y0 = int(floor(sy));
    const float fx = sx - float(x0);
    const float fy = sy - float(y0);
    const int x1 = min(x0 + 1, int(sourceWidth) - 1);
    const int y1 = min(y0 + 1, int(sourceHeight) - 1);
    const int xc = max(x0, 0);
    const int yc = max(y0, 0);
    const float4 s00 = sourceTex[uint2(xc, yc)];
    const float4 s10 = sourceTex[uint2(x1, yc)];
    const float4 s01 = sourceTex[uint2(xc, y1)];
    const float4 s11 = sourceTex[uint2(x1, y1)];
    const float4 top = lerp(s00, s10, fx);
    const float4 bottom = lerp(s01, s11, fx);
    value = lerp(top, bottom, fy);
    }
    if(encodeSrgb>2.5){value.rgb=HdrProxy(value.rgb);}
    // The dither runs in the *coded* domain, so it must be applied after the
    // transfer encode of whichever 8/10-bit target the graph is writing.
    if(encodeSrgb>1.5&&encodeSrgb<2.5){outputTex[globalId.xy]=float4(HdrEncodePq(value.rgb)+OutputDither(globalId.xy,ditherStep),value.a);return;}
    if (encodeSrgb > 0.5) {
        const float3 c = max(value.rgb, 0.0);
        value.rgb = select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0/2.4) - 0.055) + OutputDither(globalId.xy, ditherStep);
    }
    if(encodeSrgb < -0.5){const float3 c=max(value.rgb,0.0);value.rgb=select(c<=0.04045,c/12.92,pow((c+0.055)/1.055,2.4));}
    outputTex[globalId.xy] = value;
    (void)groupId;
    (void)localId;
    (void)tile;
}
