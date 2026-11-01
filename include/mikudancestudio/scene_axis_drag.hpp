#pragma once

namespace mikudancestudio::navigation {
// Native operation-panel modes 13..18. The compatibility x86 host chooses
// double intermediates; portable hosts retain the original x64 float stores.
template <class Scalar = float, class Transform>
void PositionAxis(float* camera, float* accessory, bool cameraMode, int coordinate, int parentModel,
                  unsigned axis, int dy, bool shift, bool control, Transform transform) {
    const Scalar scale = shift ? Scalar(.5f) : control ? Scalar(.005f) : Scalar(.05f);
    const Scalar delta = Scalar(dy) * scale;
    if (coordinate == 2 && cameraMode) {
        if (accessory)
            accessory[axis] = float(Scalar(accessory[axis]) - delta);
    } else if (parentModel >= 0 || coordinate == 1) {
        camera[axis] = float(Scalar(camera[axis]) + delta);
    } else {
        float local[3]{};
        local[axis] = float(delta);
        const auto world = transform(local);
        for (unsigned c = 0; c < 3; ++c)
            camera[c] += world[c];
    }
}
template <class Scalar = float>
void AngleAxis(float* camera, float* accessory, int coordinate, unsigned axis, int dy, bool shift,
               bool control) {
    const Scalar scale = shift ? Scalar(.2f) : control ? Scalar(.002f) : Scalar(.02f);
    float* target = coordinate == 2 ? accessory : camera;
    if (target)
        target[axis] = float(Scalar(target[axis]) + Scalar(dy) * scale);
}
}
