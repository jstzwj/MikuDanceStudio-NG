// Public, device-independent P5.2 parameter fixture. No execution claim.
static const int Scale = 3;
float3 Tint = float3(1, 2, 3) * (Scale + 1) / 2;
float4x4 Matrix = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
float Values[2] = {0.25f, 1e-2};
float One[1] = {9};
bool Enabled = (3 > 2) && !false;
int Truncated = int(-3.8);
static const float Large = 16777217.0;
float Referenced = Large+1.0;
float Folded = (16777216.0f+1.0f)+1.0f;
const float Writable=5;
float Mixed = 16777217 + 1.0f;
float Zero;
string Caption = "line\n\"quoted\"";
shared float SharedValue = 2;
float Local = 8;
technique Parameters {}
