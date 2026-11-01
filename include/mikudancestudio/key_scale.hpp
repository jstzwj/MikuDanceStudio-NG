#pragma once
namespace mikudancestudio::keyframe_edit {
// Original x64 9D120 uses two single-precision operations, with an identity
// fast path. Keep the operand order and retain signed-zero payloads on identity.
template <class Key> void ScaleMorphKey(Key& key, float scale, float add) {
    if (key.allocated && (scale != 1.f || add != 0.f))
        key.value = static_cast<float>(scale * key.value) + add;
}
}
