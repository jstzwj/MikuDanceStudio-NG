// CC0-1.0. Public include/macro fixture.
#ifndef MDS_PUBLIC_FX_PARAMETERS
#define MDS_PUBLIC_FX_PARAMETERS
#define BASE_VALUE 3
#ifdef MME_MIPMAP
shared float4 Tint <string UIName = "Tint";> = {0.25, 0.5, 0.75, 1};
#else
shared float4 Tint <string UIName = "Tint";> = {1, 1, 1, 1};
#endif
float4x4 WorldViewProjection : WORLDVIEWPROJECTION;
float Weights[2] = {1, 2};
bool Enabled = true;
int Counter = 3;
string Caption = "Unicode: 日本語 / 中文";
#endif
