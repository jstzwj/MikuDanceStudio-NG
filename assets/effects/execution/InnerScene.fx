struct Vertex {float4 position:POSITION;float2 uv:TEXCOORD0;};
float2 TargetViewport:VIEWPORTPIXELSIZE;
Vertex vertex(float3 p:POSITION,float2 uv:TEXCOORD0) {
 Vertex o;o.position=float4(p,1);o.uv=uv+.5/TargetViewport;return o;
}
// CC0. A second carrier wraps the base geometry inside OuterScene's capture.
float Script:STANDARDSGLOBAL <string ScriptClass="scene";
    string ScriptOrder="postprocess";string ScriptOutput="color";> = .8;
texture Captured:RENDERCOLORTARGET <string Format="A8R8G8B8";>;
texture Depth:RENDERDEPTHSTENCILTARGET <string Format="D24S8";>;
sampler Capture=sampler_state {AddressU=CLAMP;AddressV=CLAMP;Texture=<Captured>;MinFilter=POINT;MagFilter=POINT;MipFilter=NONE;};
float4 copy(float2 uv:TEXCOORD0):COLOR0{return tex2D(Capture,uv);}
technique Scene <string Script=
    "RenderColorTarget0=Captured;RenderDepthStencilTarget=Depth;"
    "Clear=Depth;Clear=Color;ScriptExternal=Color;RenderColorTarget0=;"
    "RenderDepthStencilTarget=;Clear=Color;Pass=Finish;";> {
    pass Finish <string Script="Draw=Buffer;";> {CullMode=None;ZEnable=false;ZWriteEnable=false;ZFunc=Always;
 AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
 FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
 SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
 VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 copy();}
}
