#include "editor_key_edit_native.hpp"
#include "editor_bone_scale_fixture.hpp"
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1),
        curve(original.Function<void*>(0x62bb0), &ret, 1),
        seek(original.Function<void*>(0xcbd90), &ret, 1),
        refresh(original.Function<void*>(0x20dd0), &ret, 1);
    auto child = &Child;
    auto text = &Text;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child),
        getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    float maximum = 0;
    for (const char* file : {"facial.pmd", "articulated.pmx"})
        for (const auto& q : {cpu_math::Quaternion{0, 0, 0, 1},
                              {.2f, .3f, .4f, .5f},
                              {.5f, .5f, -.5f, .5f},
                              {-.5f, .5f, .5f, .5f}})
            for (unsigned variant = 0; variant < 8; ++variant) {
                editor::EditorSession portable;
                std::string reason;
                Check(BoneScaleFixture(portable, argv[2], file, q, reason), reason.c_str());
                if (variant == 7)
                    Check(portable.SetSelectedKeys({}, reason, {{editor::Track::Bone, 0}}),
                          reason.c_str());
                Native native(original, portable);
                auto* model = native.model.get();
                std::fill(app.begin(), app.end(), 0);
                std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                int frame = portable.Document().timeline.currentFrame;
                std::memcpy(app.data() + 0x1450, &frame, 4);
                auto values = BoneScaleIdentity;
                if (variant >= 1 && variant <= 6) {
                    values[2 * (variant - 1)] = -1.25f;
                    values[2 * (variant - 1) + 1] = 35.7f;
                }
                for (unsigned i = 0; i < values.size(); ++i) {
                    char field[24];
                    std::snprintf(field, sizeof field, "%.9g", double(values[i]));
                    fields[i] = field;
                }
                original.Function<void (*)(HWND)>(0x9c9c0)(nullptr);
                Check(portable.ScaleBoneKeys(values, reason), reason.c_str());
                native.Compare(portable, maximum, variant >= 4 && variant <= 6);
                Check(app[0xa1b31], "native bone scale dirty flag");
                Check(model->UndoCursor() == (variant == 7 ? 29 : 0),
                      "native bone scale history gate");
                if (variant != 7) {
                    const auto& undo = model->undoRings[0].slots[0];
                    Check(undo.frame == 15 && undo.recordCount == 3,
                          "native selected-slot capture");
                    if (variant == 1) {
                        Check(portable.SetSelectedKeys({{editor::Track::Bone, 0, 0, 20}}, reason,
                                                       {{editor::Track::Bone, 0}}),
                              reason.c_str());
                        for (auto& key : native.keys)
                            key.allocated = 0;
                        native.keys[native.bones.size() + 1].allocated = 1;
                    }
                    for (unsigned cycle = 0; cycle < 2; ++cycle) {
                        original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcef90)(model,
                                                                                      &frame);
                        Check(portable.Undo(reason), reason.c_str());
                        native.Compare(portable, maximum);
                        original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcfb80)(model,
                                                                                      &frame);
                        Check(portable.Redo(reason), reason.c_str());
                        native.Compare(portable, maximum, variant >= 4 && variant <= 6);
                    }
                }
                ++cases;
            }
    std::printf(
        "Original x64 bone scale: %u full-pool/undo/redo cases, quaternion max error %.9g, no windows\n",
        cases, maximum);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
