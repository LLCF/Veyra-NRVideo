// Linear BT.709 working values use scRGB units: 1 = 80 cd/m2.
// Keep out-of-709-gamut negative components until the final BT.2020 boundary.
float3 HdrTo2020(float3 c) {
    return mul(float3x3(.627404,.329283,.043313,.069097,.919540,.011362,.016391,.088013,.895595),c);
}
float3 HdrTo709(float3 c) {
    return mul(float3x3(1.660491,-.587641,-.072850,-.124550,1.132900,-.008349,-.018151,-.100579,1.118730),c);
}
float3 HdrEncodePq(float3 linear709) {
    float3 p=pow(saturate(HdrTo2020(linear709)*(.008)),2610.0/16384.0);
    return pow((3424.0/4096.0+(2413.0/128.0)*p)/(1+(2392.0/128.0)*p),2523.0/32.0);
}
float3 HdrDecodePq(float3 pq) {
    float3 p=pow(saturate(pq),32.0/2523.0);
    return HdrTo709(125.0*pow(max(p-3424.0/4096.0,0)/max(2413.0/128.0-2392.0/128.0*p,1e-6),16384.0/2610.0));
}
float3 HdrProxy(float3 hdr) {
    // Fixed 203-nit reference: no per-frame exposure pump. Only the proxy is
    // gamut-limited; the original HDR base is never reconstructed from it.
    float3 c=max(hdr*(80.0/203.0),0);
    return select(c<=.75,c,.75+.25*(1-exp(-5.778*max(c-.75,0))));
}
float3 HdrRestore(float3 base,float3 proxy,float3 enhanced) {
    float3 delta=enhanced-proxy;
    float y=max(dot(base,float3(.212639,.715169,.072192)),0);
    float peak=max(max(proxy.r,proxy.g),proxy.b);
    // Preserve compressed highlights and near-black; never divide by proxy
    // luminance. Zero delta is an exact identity including wide-gamut colors.
    float gate=smoothstep(0,.02,y)*(1-smoothstep(.75,.995,peak));
    return base+clamp(delta,-.5,.5)*(203.0/80.0)*gate;
}

// Triangular (TPDF) output dither keyed by the destination pixel. Two
// independent uniform hashes give a triangular distribution instead of a
// visible ordered pattern, so the quantisation error of an 8/10-bit write
// becomes uncorrelated noise instead of banding. `step` is the target's
// quantisation step (1/255, 1/1023, ...); 0 disables the dither entirely.
float OutputDither(uint2 pixel, float step)
{
    if (step <= 0.0) return 0.0;
    uint h1 = pixel.x * 1973u + pixel.y * 9277u + 26699u;
    h1 ^= h1 >> 16u; h1 *= 0x7feb352du; h1 ^= h1 >> 15u; h1 *= 0x846ca68bu; h1 ^= h1 >> 16u;
    uint h2 = pixel.x * 1327u + pixel.y * 4591u + 101u;
    h2 ^= h2 >> 15u; h2 *= 0x2c1b3c6du; h2 ^= h2 >> 12u; h2 *= 0x297a2d39u; h2 ^= h2 >> 15u;
    const float r1 = float(h1 & 0xFFFFFFu) / 16777215.0;
    const float r2 = float(h2 & 0xFFFFFFu) / 16777215.0;
    return (r1 - r2) * step;
}
