// Behavioral subset of skin_effect_sm3.fx: ZValuePlot and BufferShadow /
// BShadowTexture / BShadowSphia(Texture), including separate material tint pools.
cbuffer BuiltinShadow : register(b2) {
    row_major float4x4 shadowLight, shadowCamera, shadowRotation;
    float4 shadowColor, shadowToon, shadowSpecular, shadowDirection, shadowEye;
    uint4 shadowOptions; // mode, perspective, transparent, material stage roles
    float4 shadowDimensions;
    float4 textureMultiply, textureAdd, sphereMultiply, sphereAdd;
    row_major float4x4 accessoryWorld;
    float4 accessoryDiffuse;
};
Texture2D materialTextures[3] : register(t0);
SamplerState materialSamplers[3] : register(s0);
Texture2D shadowTexture : register(t3);
SamplerState shadowSampler : register(s3);
cbuffer Cascade : register(b0) {
    uint4 unusedStages[3];
    uint4 alphaTest;
};
float4 shadow_output(float4 color) {
    if (alphaTest.x) {
        float value = floor(saturate(color.a)*255+.5), reference = alphaTest.z;
        bool accepted = alphaTest.y == 7 ||
            (alphaTest.y == 1 && value < reference) || (alphaTest.y == 2 && value == reference) ||
            (alphaTest.y == 3 && value <= reference) || (alphaTest.y == 4 && value > reference) ||
            (alphaTest.y == 5 && value != reference) || (alphaTest.y == 6 && value >= reference);
        if (!accepted) discard;
    }
    return color;
}
struct ShadowVertex {
    float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0;
};
struct ShadowPixel {
    float4 position : SV_POSITION;
    float4 light : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 eye : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float2 sphere : TEXCOORD4;
    float4 color : COLOR0;
    float2 uv1 : TEXCOORD5;
};
ShadowPixel shadow_vs(ShadowVertex v) {
    ShadowPixel o;
    o.light = mul(float4(v.position,1),shadowLight);
    o.position = shadowOptions.x == 1 ? o.light : mul(float4(v.position,1),shadowCamera);
    o.normal = v.normal; o.eye = shadowEye.xyz-v.position;
    o.uv = v.uv;
    o.uv1 = 0;
    o.sphere = mul(float4(v.normal,0),shadowRotation).xy;
    o.color = shadowColor;
    if (shadowOptions.w & 262144) {
        o.normal = mul(float4(v.normal,0),accessoryWorld).xyz;
        o.eye = shadowEye.xyz-mul(float4(v.position,1),accessoryWorld).xyz;
        o.color = saturate(shadowColor+max(0,accessoryDiffuse*dot(normalize(o.normal),-shadowDirection.xyz)));
        o.color.a = shadowColor.a;
    }
    return o;
}
ShadowPixel shadow_uv_vs(float3 position : POSITION, float3 normal : NORMAL,
                         float2 uv : TEXCOORD0, float4 uv1 : TEXCOORD1) {
    ShadowVertex v; v.position=position; v.normal=normal; v.uv=uv;
    ShadowPixel o=shadow_vs(v); o.uv1=uv1.xy;
    return o;
}
ShadowPixel shadow_outline_vs(float3 position : POSITION) {
    ShadowPixel o = (ShadowPixel)0;
    o.position = mul(float4(position,1),shadowCamera);
    return o;
}
float4 shadow_ps(ShadowPixel p) : SV_TARGET {
    if (shadowOptions.x == 1) return shadow_output(float4(p.light.z/p.light.w,0,0,1));
    if (shadowOptions.x == 3) return shadow_output(shadowColor);
    float3 normal = normalize(p.normal);
    float3 specular = saturate(pow(max(0,dot(normal,normalize(-shadowDirection.xyz+normalize(p.eye)))),shadowSpecular.w)*shadowSpecular.rgb);
    float4 light = p.light/p.light.w;
    float2 uv = float2((1+light.x)*.5,(1-light.y)*.5);
    bool outside = uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1;
    float comp = 1;
    if (!outside) {
        float difference = (shadowOptions.w & 65536) ? max(light.z-shadowTexture.Sample(shadowSampler,uv).r,0) : 0;
        comp = 1-saturate(shadowOptions.y ? difference*8000*uv.y-.3 : difference*1500-.3);
        if (!(shadowOptions.w & 262144))
            comp = min(saturate(dot(normal,-shadowDirection.xyz)*3),comp);
    }
    if (shadowOptions.w & 262144) {
        float4 lit = p.color, ambient = shadowColor;
        float alpha = p.color.a;
        bool sphere = false;
        float2 sphereUv = float2(p.sphere.x*.5+.5,p.sphere.y*-.5+.5);
        [unroll] for (uint i=0;i<3;++i) {
            uint role = (shadowOptions.w >> (i*4)) & 15;
            if (role == 1) {
                float4 sample = materialTextures[i].Sample(materialSamplers[i],p.uv);
                lit *= sample; ambient *= sample; alpha *= sample.a;
            } else if (role == 2 || role == 3) {
                float4 sample = materialTextures[i].Sample(materialSamplers[i],sphereUv);
                lit = role == 3 ? lit+sample : lit*sample;
                ambient = role == 3 ? ambient+sample : ambient*sample;
                sphere = true;
            }
        }
        float4 result = lit+float4(specular,0);
        if (!outside && comp != 1) {
            result = result*comp+ambient*(1-comp);
            // Native sphere effects override alpha only in their shadow branch.
            if (sphere) result.a = alpha;
        }
        return shadow_output(result);
    }
    float4 color = shadowColor;
    float alpha = color.a;
    bool baseTexture = false;
    bool ordinaryBase = false, subtexture = false;
    float2 sphereUv = float2(p.sphere.x*.5+.5,p.sphere.y*-.5+.5);
    [unroll] for (uint i=0;i<3;++i) {
        uint role = (shadowOptions.w >> (i*4)) & 15;
        if (role == 1) {
            float4 sample = materialTextures[i].Sample(materialSamplers[i],p.uv);
            float4 tinted = sample;
            if (shadowOptions.w & 131072) {
                float weight = textureMultiply.w + textureAdd.w;
                tinted = (sample*textureMultiply+textureAdd)*weight+(1-weight);
            }
            color *= tinted; alpha *= sample.a; baseTexture = true;
            ordinaryBase = true;
        } else if (role == 2 || role == 3 || role == 4) {
            float4 sample = materialTextures[i].Sample(materialSamplers[i],role==4?p.uv1:sphereUv);
            float4 tinted = sample;
            if (shadowOptions.w & 131072) {
                float weight = sphereMultiply.w + sphereAdd.w;
                tinted = (sample*sphereMultiply+sphereAdd)*weight;
                if (role != 3) tinted += 1-weight;
            }
            color.rgb = role == 3 ? color.rgb+tinted.rgb : color.rgb*tinted.rgb;
            alpha *= sample.a;
            if (role == 4) { baseTexture = true; subtexture = true; }
        }
    }
    float3 result;
    if (outside || comp == 1) result = color.rgb+specular;
    else if (baseTexture) result = color.rgb*(comp+shadowToon.rgb*(1-comp))+specular*comp;
    else result = color.rgb*comp+color.rgb*shadowToon.rgb*(1-comp)+specular*comp;
    // BShadowTextureTexCd2Tec retains the full float4 toon multiplication.
    // Its apparent alpha reassignment is an expression without an assignment.
    if (ordinaryBase && subtexture && !outside && comp != 1)
        alpha *= comp+shadowToon.a*(1-comp);
    return shadow_output(float4(result,shadowOptions.z && !outside ? .5 : alpha));
}
