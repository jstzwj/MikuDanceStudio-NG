// CC0-1.0. Public P5 source/metadata fixture. No NG-specific annotations.
#include "parameters.fxh"
#define PRODUCT(a,b) ((a)*(b))
#define JOIN(a,b) a##b
float Script : STANDARDSGLOBAL <
    string ScriptClass = "scene";
    string ScriptOrder = "postprocess";
    string ScriptOutput = "color";
> = 0.8;
float JOIN(Val,ue) = PRODUCT(BASE_VALUE, 2);
float Tag : _INDEX;
texture ColorTarget : RENDERCOLORTARGET <
    float2 ViewportRatio = {0.5, 0.5};
    string Format = "A16B16G16R16F";
    string DefaultEffect = "self = hide;";
>;
sampler ColorSampler = sampler_state {
    Texture = <ColorTarget>;
    MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = POINT;
    AddressU = CLAMP; AddressV = WRAP;
};
technique Main <string MmdPass = "object"; string Script =
    "RenderColorTarget0=ColorTarget;" "Pass=Capture;" "ScriptExternal=Color;";
> {
    pass Capture <string Script = "Draw=Buffer;";> { CullMode = NONE; }
    pass Final { AlphaBlendEnable = FALSE; }
}
