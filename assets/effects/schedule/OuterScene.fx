// CC0. Outer postprocess, shared publisher, DefaultEffect parent and loop.
float Script:STANDARDSGLOBAL <string ScriptClass="scene";
    string ScriptOrder="postprocess";string ScriptOutput="color";> = .8;
shared texture ParentMap:OFFSCREENRENDERTARGET <
    string Format="A8R8G8B8";int Miplevels=0;
    float4 ClearColor={0,0,0,0};float ClearDepth=1;
    string DefaultEffect="leaf.pmx=Child.fx;*=hide;";
>;
texture Captured:RENDERCOLORTARGET <string Format="A8R8G8B8";>;
texture Depth:RENDERDEPTHSTENCILTARGET <string Format="D24S8";>;
int Count=2;
int Index=9;
float2 TargetSize:VIEWPORTPIXELSIZE;
sampler Capture=sampler_state {AddressU=CLAMP;AddressV=CLAMP;Texture=<Captured>;MinFilter=POINT;MagFilter=POINT;MipFilter=NONE;};
float4 copy(float2 uv:TEXCOORD0):COLOR0{return tex2D(Capture,uv);}
technique Scene <string Script=
    "RenderColorTarget0=Captured;RenderDepthStencilTarget=Depth;"
    "Clear=Color;Clear=Depth;ScriptExternal=Color;RenderColorTarget0=;"
    "RenderDepthStencilTarget=;Clear=Color;LoopByCount=Count;LoopGetIndex=Index;"
    "Pass=Finish;LoopEnd=;";> {
    pass Finish <string Script="Draw=Buffer;";> {PixelShader=compile ps_3_0 copy();}
}
