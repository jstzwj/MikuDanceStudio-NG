struct Vertex {float4 position:POSITION;float2 uv:TEXCOORD0;};
float2 TargetViewport:VIEWPORTPIXELSIZE;
Vertex vertex(float3 p:POSITION,float2 uv:TEXCOORD0) {
 Vertex o;o.position=float4(p,1);o.uv=uv+.5/TargetViewport;return o;
}
// CC0. Reader-only shared declaration binds the live outer scene publisher.
shared texture ParentMap:OFFSCREENRENDERTARGET;
sampler Parent=sampler_state {AddressU=CLAMP;AddressV=CLAMP;Texture=<ParentMap>;MinFilter=LINEAR;MagFilter=LINEAR;MipFilter=LINEAR;};
float4 shade(float2 uv:TEXCOORD0):COLOR0{return tex2Dlod(Parent,float4(uv,0,1));}
technique Main <string MmdPass="object";> {
    pass Paint {CullMode=None;ZEnable=false;ZWriteEnable=false;ZFunc=Always;
 AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
 FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
 SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
 VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 shade();}
}
