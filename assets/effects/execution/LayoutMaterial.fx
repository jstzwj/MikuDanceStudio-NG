// CC0. Explicit constant layout and all sixteen live FX9 sampler slots.
float a=.1;float2 b={.2,.3};float one[1]={.4};float after=.5;
bool enabled=true;int count=2;float weights[2]={.25,.75};
float4x4 worldViewProjection:WORLDVIEWPROJECTION;
const float host=.5;static const float privateValue=.5;
texture Image:MATERIALTEXTURE;
struct Vertex {float4 position:POSITION;float2 uv:TEXCOORD0;};
Vertex vertex(float3 p:POSITION,float2 uv:TEXCOORD0) {
 Vertex o;o.position=mul(float4(p,1),worldViewProjection);o.uv=uv;return o;
}
sampler S0=sampler_state {Texture=<Image>;MinFilter=ANISOTROPIC;MagFilter=ANISOTROPIC;MaxAnisotropy=16;
 MipFilter=LINEAR;AddressU=CLAMP;AddressV=CLAMP;};
sampler S1=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S2=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S3=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S4=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S5=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S6=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S7=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S8=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S9=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S10=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S11=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S12=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S13=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S14=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler S15=sampler_state {Texture=<Image>;MinFilter=POINT;MagFilter=POINT;
 MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
float4 pixel(Vertex input):COLOR0{return (tex2D(S0,input.uv)+tex2D(S1,input.uv)+tex2D(S2,input.uv)+tex2D(S3,input.uv)+tex2D(S4,input.uv)+tex2D(S5,input.uv)+tex2D(S6,input.uv)+tex2D(S7,input.uv)+tex2D(S8,input.uv)+tex2D(S9,input.uv)+tex2D(S10,input.uv)+tex2D(S11,input.uv)+tex2D(S12,input.uv)+tex2D(S13,input.uv)+tex2D(S14,input.uv)+tex2D(S15,input.uv))/16*
 float4((a+b.x+b.y+one[0])*.25+worldViewProjection[3].x,after+worldViewProjection[3].y,(weights[0]+weights[1])*.75,
 enabled && count==2 ? host*privateValue*2 : 0);}
technique Main <string MMDPass="object";> {pass Paint {CullMode=None;ZEnable=false;ZWriteEnable=false;ZFunc=Always;
 AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
 FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
 SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
 VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 pixel();}}
