// CC0. Reader-only shared declaration binds the live outer scene publisher.
shared texture ParentMap:OFFSCREENRENDERTARGET;
sampler Parent=sampler_state {AddressU=CLAMP;AddressV=CLAMP;Texture=<ParentMap>;MinFilter=LINEAR;MagFilter=LINEAR;MipFilter=LINEAR;};
float4 shade(float2 uv:TEXCOORD0):COLOR0{return tex2D(Parent,uv);}
technique Main <string MmdPass="object";> {
    pass Paint {PixelShader=compile ps_3_0 shade();}
}
