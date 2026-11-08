#include "Common.fxh"
float Script:STANDARDSGLOBAL <string ScriptClass="scene";
    string ScriptOrder="postprocess";string ScriptOutput="color";> = .8;
float Exposure=1;
float4 SceneClear={1,1,1,0};
float2 Size:VIEWPORTPIXELSIZE;
texture Captured:RENDERCOLORTARGET <string Format="A8R8G8B8";>;
texture Intermediate:RENDERCOLORTARGET <string Format="A8R8G8B8";>;
sampler Source=sampler_state {Texture=<Captured>;MinFilter=POINT;MagFilter=POINT;MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler Next=sampler_state {Texture=<Intermediate>;MinFilter=POINT;MagFilter=POINT;MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
Vertex vertex(float3 p:POSITION,float2 uv:TEXCOORD0) {Vertex o;o.position=float4(p,1);o.uv=uv+.5/Size;return o;}
float4 copy(Vertex v):COLOR0 {return tex2D(Source,v.uv);}
float4 finish(Vertex v):COLOR0 {float4 c=tex2D(Next,v.uv);return float4(c.rgb*Exposure,c.a);}
technique Scene <string Script="RenderColorTarget0=Captured;ClearSetColor=SceneClear;Clear=Color;Clear=Depth;ScriptExternal=Color;RenderColorTarget0=Intermediate;RenderDepthStencilTarget=;Clear=Color;Pass=Copy;RenderColorTarget0=;Clear=Color;Pass=Finish;";> {
    pass Copy <string Script="Draw=Buffer;";> {
        CullMode=None;ZEnable=false;ZWriteEnable=false;ZFunc=Always;
        AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
        FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
        SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
        VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 copy();
    }
    pass Finish <string Script="Draw=Buffer;";> {
        CullMode=None;ZEnable=false;ZWriteEnable=false;ZFunc=Always;
        AlphaTestEnable=false;AlphaBlendEnable=false;StencilEnable=false;
        FillMode=Solid;DepthBias=0;SlopeScaleDepthBias=0;ColorWriteEnable=15;
        SRGBWriteEnable=false;SeparateAlphaBlendEnable=false;BlendOp=Add;
        VertexShader=compile vs_3_0 vertex();PixelShader=compile ps_3_0 finish();
    }
}
