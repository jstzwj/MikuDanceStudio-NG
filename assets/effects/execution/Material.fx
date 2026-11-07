struct Vertex {float4 position:POSITION;float2 uv:TEXCOORD0;};
float2 TargetViewport:VIEWPORTPIXELSIZE;
Vertex vertex(float3 p:POSITION,float2 uv:TEXCOORD0) {
 Vertex o;o.position=float4(p,1);o.uv=uv+.5/TargetViewport;return o;
}
// CC0. Original minimal material for raw FX to GPU execution qualification.
float4 Tint={.25,.5,.75,1};
float2 TargetSize:VIEWPORTPIXELSIZE;
string Label="public\nmaterial";
float4 shade(float2 uv:TEXCOORD0):COLOR0 {return Tint;}
technique Textured <string MmdPass="object";bool UseTexture=true;string Subset="0-2";> {
    pass Paint {CullMode=None;ZEnable=false;ZWriteEnable=false;ZFunc=Always;
 AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
 FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
 SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
 VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 shade();}
}
technique Plain <string MmdPass="object";> {
    pass Paint {CullMode=None;ZEnable=false;ZWriteEnable=false;ZFunc=Always;
 AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
 FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
 SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
 VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 shade();}
}
technique EmptyEdge <string MmdPass="edge";> {}
