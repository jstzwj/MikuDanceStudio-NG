// CC0. Original minimal material for CPU/Null scheduling qualification.
float4 Tint={.25,.5,.75,1};
float2 TargetSize:VIEWPORTPIXELSIZE;
string Label="public\nmaterial";
float4 shade(float2 uv:TEXCOORD0):COLOR0 {return Tint;}
technique Textured <string MmdPass="object";bool UseTexture=true;string Subset="0-2";> {
    pass Paint {PixelShader=compile ps_3_0 shade();}
}
technique Plain <string MmdPass="object";> {
    pass Paint {PixelShader=compile ps_3_0 shade();}
}
technique EmptyEdge <string MmdPass="edge";> {}
