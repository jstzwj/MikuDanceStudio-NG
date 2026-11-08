#include "Common.fxh"
float4 Tint={.25,.5,.75,1};
float4x4 WorldViewProjection:WORLDVIEWPROJECTION;
Vertex vertex(float3 p:POSITION,float2 uv:TEXCOORD0) {
    Vertex o;o.position=mul(float4(p,1),WorldViewProjection);o.uv=uv;return o;
}
float4 pixel(Vertex v):COLOR0 {return Tint;}
technique Main <string MMDPass="object";> {
    pass Paint {
        CullMode=None;ZEnable=true;ZWriteEnable=true;ZFunc=LessEqual;
        AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
        FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
        SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
        VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 pixel();
    }
}
