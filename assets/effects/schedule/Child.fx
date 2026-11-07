// CC0. A child assignment publishes and then samples its own descendant turn.
texture ChildMap:OFFSCREENRENDERTARGET <
    string Format="A8R8G8B8";float2 ViewportRatio={.5,.5};int Miplevels=0;
    float4 ClearColor={0,0,0,0};float ClearDepth=1;
    string DefaultEffect="leaf.pmx=Material.fx;*=hide;";
>;
sampler ChildSampler=sampler_state {
    AddressU=CLAMP;AddressV=CLAMP;Texture=<ChildMap>;MinFilter=LINEAR;MagFilter=LINEAR;MipFilter=LINEAR;
};
float4 shade(float2 uv:TEXCOORD0):COLOR0{return tex2D(ChildSampler,uv);}
technique Child <string MmdPass="object";> {
    pass Paint {PixelShader=compile ps_3_0 shade();}
}
