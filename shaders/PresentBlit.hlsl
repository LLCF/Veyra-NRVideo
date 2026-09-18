// PresentBlit: pixel-shader blit of the working frame onto a flip-model
// swapchain back buffer. Flip back buffers may only transition between
// PRESENT and RENDER_TARGET (not UAV), so the present path is a fullscreen
// triangle sample instead of a compute write.

#include "HdrColor.hlsli"
Texture2D<float4> sourceTex : register(t0);

cbuffer PresentBlitConstants : register(b0)
{
    float4 srcDims; // x/y=content extent z=flags (1 encode, 2 fine sampling) w=zoom
    float4 dstDims; // x/y=client extent z/w=view center
};

SamplerState linearClamp : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut vsMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

float4 CubicWeights(float t)
{
    float t2=t*t,t3=t2*t;
    return float4(-0.5*t+t2-0.5*t3,1.0-2.5*t2+1.5*t3,
                  0.5*t+2.0*t2-1.5*t3,-0.5*t2+0.5*t3);
}

float4 FineSample(float2 uv)
{
    uint width,height;sourceTex.GetDimensions(width,height);
    float2 p=uv*float2(width,height)-0.5;
    // A cubic magnifier is not an antialiased minifier. Preserve the legacy
    // minification path instead of applying an undersized kernel to downscale.
    float footprint=max(length(ddx(p)),length(ddy(p)));
    if(footprint>1.001)return sourceTex.SampleLevel(linearClamp,uv,0);
    int2 last=int2(width,height)-1;
    if(all(abs(p-round(p))<0.0005))return sourceTex.Load(int3(clamp(int2(round(p)),int2(0,0),last),0));
    int2 base=int2(floor(p));float2 t=frac(p);
    float4 wx=CubicWeights(t.x),wy=CubicWeights(t.y);
    float4 result=0;
    [unroll]for(int y=0;y<4;++y){
        [unroll]for(int x=0;x<4;++x){
            int2 q=clamp(base+int2(x-1,y-1),int2(0,0),last);
            result+=sourceTex.Load(int3(q,0))*wx[x]*wy[y];
        }
    }
    // Bound cubic lobes to the local 2x2 neighborhood to avoid bright/dark
    // halos. Keep negative/HDR values when present in the actual neighborhood.
    float4 a=sourceTex.Load(int3(clamp(base,int2(0,0),last),0));
    float4 b=sourceTex.Load(int3(clamp(base+int2(1,0),int2(0,0),last),0));
    float4 c=sourceTex.Load(int3(clamp(base+int2(0,1),int2(0,0),last),0));
    float4 d=sourceTex.Load(int3(clamp(base+int2(1,1),int2(0,0),last),0));
    return clamp(result,min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
}

float4 psMain(VSOut input) : SV_Target
{
    // zoom==0 retains the original contract for diagnostic harness callers.
    float2 uv=input.uv;
    if(srcDims.w>0){
        float fit=min(dstDims.x/srcDims.x,dstDims.y/srcDims.y)*srcDims.w;
        uv=(input.uv-.5)*dstDims.xy/(srcDims.xy*fit)+dstDims.zw;
        if(any(uv<0)||any(uv>1))return float4(0,0,0,1);
    }
    uint flags=uint(srcDims.z+0.5);
    float4 color=(flags&2)?FineSample(uv):sourceTex.SampleLevel(linearClamp,uv,0);
    if(flags&8)color.rgb*=203.0/80.0; // SDR comparison white in an HDR output.
    if(flags&1){float3 c=max(color.rgb,0);color.rgb=lerp(1.055*pow(c,1.0/2.4)-0.055,c*12.92,step(c,0.0031308));}
    if(flags&4)color.rgb=HdrEncodePq(color.rgb);
    return color;
}
