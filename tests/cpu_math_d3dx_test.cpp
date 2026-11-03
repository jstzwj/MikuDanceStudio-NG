// Independent Windows CPU reference. No HWND or D3D device is created.
#include "mikudancestudio/cpu_math.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/render_model_inputs.hpp"
#include "../src/render/rhi_model_constants.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace mikudancestudio;
using namespace cpu_math;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
bool Equal(const float* a,const float* b,unsigned count,float tolerance=4e-6f) {
    for(unsigned n=0;n<count;++n)
        if(!std::isfinite(a[n]) || !std::isfinite(b[n]) ||
           std::abs(a[n]-b[n])>tolerance*std::max(1.f,std::abs(b[n]))) {
            std::fprintf(stderr,"component=%u portable=%.9g D3DX=%.9g\n",n,a[n],b[n]);return false;
        }
    return true;
}
int main() {
    // Independent native observations for the shared fixed-function transport.
    // The aligned x64 routine uses different sum trees for rows 0/3 and 1/2.
    unsigned products = 0;
#if defined(_M_X64)
    for (unsigned sample = 0; sample < 64; ++sample) {
        Matrix a{}, b{};
        for (unsigned i = 0; i < 16; ++i) {
            a[i] = float(int((sample * 31 + i * 17) % 109) - 54) / 7;
            b[i] = float(int((sample * 47 + i * 13) % 127) - 63) / 11;
        }
        d3dx::D3DXMATRIXF nativeA, nativeB, nativeProduct;
        std::memcpy(&nativeA, a.data(), 64);
        std::memcpy(&nativeB, b.data(), 64);
        d3dx::Get().multiply(&nativeProduct, &nativeA, &nativeB);
        const auto actual = rhi::detail::Dx9MatrixMultiply(a, b);
        CHECK(Equal(actual.data(), &nativeProduct.m[0][0], 16, 0));
        ++products;
    }
    // The X loader performs this identity coordinate transform even for a
    // top-level static mesh. It is not equivalent to copying input bytes.
    D3DXMATRIX identity;
    const auto unit = Identity();
    std::memcpy(&identity, unit.data(), 64);
    for (const Vec3& p : {Vec3{4, 0, 0}, Vec3{0, 4, 0}, Vec3{-2, .5f, 1}, Vec3{1, 2, 3},
                          Vec3{.125f, -.625f, 100}}) {
        D3DXVECTOR3 input{p[0], p[1], p[2]}, output;
        CHECK(D3DXVec3TransformCoord(&output, &input, &identity));
        const Vec3 imported{p[0] * 0x1.fffffep-1f, p[1] * 0x1.fffffep-1f, p[2] * 0x1.fffffep-1f};
        CHECK(Equal(imported.data(), &output.x, 3, 0));
    }
#endif
    unsigned cameras = 0;
    for (float distance : {-5.f, -45.f, -100.f})
        for (const Vec3& eye :
             {Vec3{0, 0, distance}, Vec3{2, 3, distance}, Vec3{-12.5f, 7.25f, distance}})
            for (float fov : {1.f, 30.f, 45.f, 60.f, 120.f, 179.f})
                for (float aspect : {.5f, 1.f, 16.f / 9}) {
                    render::ModelFrameInputs frame;
                    frame.world = frame.lightRotation = Identity();
                    frame.scene.lightState = {.6f, .6f, .6f, -.5f, -1, .5f};
                    frame.view.editorNavigation = true;
                    frame.view.editorEye = eye;
                    frame.view.editorFov = fov;
                    frame.view.aspect = aspect;
                    frame.view.viewport = {0, 0, 512, 512, 0, 1};
                    rhi::ModelPipeline pipeline;
                    CHECK(render::BuildModelInputs(frame, {}, {}, pipeline));
                    const Vec3 at{eye[0], eye[1], eye[2] + 80}, up{0, 1, 0};
                    d3dx::D3DXMATRIXF view, projection;
                    d3dx::Get().lookAtLH(&view, eye.data(), at.data(), up.data());
                    d3dx::Get().perspectiveFovLH(&projection, fov * .01745329238474369f, aspect, 1,
                                                 100000);
                    CHECK(Equal(pipeline.shader.view.data(), &view.m[0][0], 16, 0));
                    CHECK(Equal(pipeline.shader.projection.data(), &projection.m[0][0], 16, 0));
                    ++cameras;
                }
    for (int sample=0;sample<64;++sample) {
        Quaternion q; CHECK(Normalize(Quaternion{.01f*sample,.2f,-.3f,1},q));
        auto rotation=RotationQuaternion(q);
        d3dx::D3DXMATRIXF nativeRotation, nativeA, nativeB, nativeProduct;
        d3dx::Get().matrixRotationQuaternion(&nativeRotation,q.data());
        CHECK(Equal(rotation.data(),&nativeRotation.m[0][0],16));
        Quaternion nativeQuaternion;
        d3dx::Get().quatFromMatrix(nativeQuaternion.data(),&nativeRotation);
        CHECK(Equal(QuaternionFromMatrix(rotation).data(),nativeQuaternion.data(),4));
        const float angle=.1f*sample;
        for (int axis=0;axis<3;++axis) {
            const auto portable=axis==0?RotationX(angle):axis==1?RotationY(angle):RotationZ(angle);
            if(axis==0)d3dx::Get().rotX(&nativeRotation,angle);
            else if(axis==1)d3dx::Get().rotY(&nativeRotation,angle);
            else d3dx::Get().rotZ(&nativeRotation,angle);
            CHECK(Equal(portable.data(),&nativeRotation.m[0][0],16));
            d3dx::Get().quatFromMatrix(nativeQuaternion.data(),&nativeRotation);
            CHECK(Equal(QuaternionFromMatrix(portable).data(),nativeQuaternion.data(),4));
        }
        auto a=Multiply(rotation,Translation({.25f*sample,-2,3}));
        auto b=Multiply(Translation({1,3,-2}),RotationQuaternion({.5f,0,0,.8660254f}));
        std::memcpy(&nativeA,a.data(),64); std::memcpy(&nativeB,b.data(),64);
        d3dx::Get().multiply(&nativeProduct,&nativeA,&nativeB);
        CHECK(Equal(Multiply(a,b).data(),&nativeProduct.m[0][0],16));
        Matrix inverse; CHECK(Inverse(a,inverse));
        D3DXMATRIX referenceInverse;
        CHECK(D3DXMatrixInverse(&referenceInverse,nullptr,reinterpret_cast<D3DXMATRIX*>(&nativeA)));
        CHECK(Equal(inverse.data(),&referenceInverse.m[0][0],16,2e-5f));
        Quaternion other; CHECK(Normalize(Quaternion{.4f,.1f,.02f*sample,1},other));
        Quaternion referenceProduct;
        d3dx::Get().quatMultiply(referenceProduct.data(),other.data(),q.data());
        CHECK(Equal(HamiltonProduct(q,other).data(),referenceProduct.data(),4));
    }
    d3dx::D3DXMATRIXF zero{};Quaternion nativeZero;
    d3dx::Get().quatFromMatrix(nativeZero.data(),&zero);
    CHECK(Equal(QuaternionFromMatrix(Matrix{}).data(),nativeZero.data(),4));
    std::puts("64 portable matrix/quaternion cases agree with independent D3DX CPU imports");
    std::printf("%u exact fixed-axis editor view/projection comparisons against D3DX CPU imports\n",
                cameras);
    std::printf("%u exact shared matrix products against D3DX CPU imports\n", products);
    return 0;
}
