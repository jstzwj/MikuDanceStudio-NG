#include "editor_model_save_fixture.hpp"
#include "mikudancestudio/pmd_writer.hpp"
#include <intrin.h>
namespace {
std::vector<std::uint8_t> enhancedNativeBytes;
bool enhancedNativeError = false, enhancedNativeAccepted = true;
unsigned enhancedNativeOpens = 0, enhancedNativeCloses = 0;
std::wstring enhancedNativePath;
void* enhancedTextureWriteReturn = nullptr;
int __cdecl EnhancedOpen(int* descriptor, const wchar_t*, int, int, int) {
    ++enhancedNativeOpens;
    *descriptor = 543;
    return enhancedNativeError ? 13 : 0;
}
int __cdecl EnhancedWrite(int descriptor, const void* data, unsigned size) {
    Check(descriptor == 543, "native PMD descriptor");
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    enhancedNativeBytes.insert(enhancedNativeBytes.end(), bytes, bytes + size);
    if (_ReturnAddress() == enhancedTextureWriteReturn) {
        Check(size == 20, "native fixed material filename width");
        // The original converts into uninitialized stack storage, then writes
        // all 20 bytes. Bytes after the first NUL are undefined stack residue,
        // not filename data. Canonicalize only that suffix for comparison.
        auto begin = enhancedNativeBytes.end() - 20;
        const auto zero = std::find(begin, enhancedNativeBytes.end(), 0);
        if (zero != enhancedNativeBytes.end())
            std::fill(zero + 1, enhancedNativeBytes.end(), 0);
    }
    return int(size);
}
int __cdecl EnhancedClose(int descriptor) {
    Check(descriptor == 543, "native PMD close descriptor");
    ++enhancedNativeCloses;
    return 0;
}
BOOL WINAPI EnhancedSelect(LPOPENFILENAMEW options) {
    Check(options->Flags == 6 && options->nFilterIndex == 1 && options->nMaxFile == 256,
          "native enhanced save flags");
    Check(std::wstring(options->lpstrDefExt) == L"pmd" &&
              std::wstring(options->lpstrInitialDir) == L"UserFile\\Model" &&
              std::wstring(options->lpstrTitle) == L"save enhanced model",
          "native enhanced save defaults");
    Check(enhancedNativePath.size() < options->nMaxFile, "native enhanced save path capacity");
    std::copy(enhancedNativePath.begin(), enhancedNativePath.end(), options->lpstrFile);
    options->lpstrFile[enhancedNativePath.size()] = 0;
    return enhancedNativeAccepted;
}
struct NativeEnhancedModel {
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::PmdVertex> vertices;
    std::vector<std::int32_t> indices;
    std::vector<mdl::ModelMaterialRecord> materials;
    std::vector<mdl::BoneRecord> bones;
    std::vector<mdl::IkChain> ik;
    std::vector<std::vector<std::uint16_t>> links;
    std::vector<mdl::MorphRecord> morphs;
    std::vector<std::vector<mdl::PmdVertexMorphEntry>> offsets;
    std::vector<mdl::DisplayGroup> groups;
    std::vector<mdl::FrameGroup> facial, boneRows;
    std::vector<mdl::RigidRecord> rigids;
    std::vector<mdl::JointRecord> joints;
    explicit NativeEnhancedModel(const PmdDocument& d)
        : vertices(d.vertices.size()), indices(d.indices.begin(), d.indices.end()),
          materials(d.materials.size()), bones(d.bones.size()), ik(d.ikChains.size()),
          links(d.ikChains.size()), morphs(d.morphs.size()), offsets(d.morphs.size()),
          groups(d.groupNames.size() + (d.facialTargets.empty() ? 2 : 3)),
          facial(d.facialTargets.size()), boneRows(d.boneFrames.size()), rigids(d.rigids.size()),
          joints(d.joints.size()) {
        std::memcpy(model->name, d.header.name.data(), 20);
        std::memcpy(model->comment, d.header.comment.data(), 256);
        std::memcpy(model->nameEn, d.englishModel.name, 20);
        std::memcpy(model->commentEn, d.englishModel.comment, 256);
        std::wcscpy(model->modelDirectory, L"C:\\fixture\\");
        model->physicsFlags = 1;
        for (unsigned i = 0; i < vertices.size(); ++i) {
            const auto& from = d.vertices[i];
            auto& to = vertices[i];
            std::memcpy(to.position, from.position, sizeof to.position);
            std::memcpy(to.normal, from.normal, sizeof to.normal);
            std::memcpy(to.uv, from.uv, sizeof to.uv);
            to.bone[0] = std::int16_t(from.bone[0]);
            to.bone[1] = std::int16_t(from.bone[1]);
            to.weightPercent = std::int8_t(from.weightPercent);
            to.edgeDisabled = from.edgeDisabled;
        }
        for (unsigned i = 0; i < materials.size(); ++i) {
            const auto& from = d.materials[i];
            auto& to = materials[i];
            std::memcpy(to.diffuse, from.diffuse, sizeof to.diffuse);
            std::memcpy(to.specular, from.specular, sizeof to.specular);
            std::memcpy(to.ambient, from.ambient, sizeof to.ambient);
            to.specularPower = from.specularPower;
            to.toonReference = from.toonReference;
            to.doubleSided = from.flags;
            to.faceVertexCount = from.faceVertexCount;
            const std::string combined(from.pmdTexture);
            const auto star = combined.find('*');
            const auto texture = combined.substr(0, star);
            const auto sphere =
                star == std::string::npos ? std::string() : combined.substr(star + 1);
            if (!texture.empty() || !sphere.empty()) {
                const auto path = std::wstring(model->modelDirectory) +
                                  std::wstring(texture.begin(), texture.end());
                std::wcscpy(to.texturePath, path.c_str());
            }
            if (!sphere.empty()) {
                const auto path = std::wstring(model->modelDirectory) +
                                  std::wstring(sphere.begin(), sphere.end());
                std::wcscpy(to.spherePath, path.c_str());
            }
        }
        for (unsigned i = 0; i < bones.size(); ++i) {
            const auto& from = d.bones[i];
            auto& to = bones[i];
            std::memcpy(to.name, from.pmdName, sizeof to.name);
            std::memcpy(to.nameEn, d.englishBoneNames[i].data(), sizeof to.nameEn);
            to.parent = from.parent;
            to.tailBone = from.tailBone;
            to.tailIdx = from.tailIdx;
            to.type = mdl::BoneType(from.type);
            std::memcpy(to.position, from.position, sizeof to.position);
        }
        for (unsigned i = 0; i < ik.size(); ++i) {
            const auto& from = d.ikChains[i];
            links[i] = from.links;
            ik[i].boneIndex = from.boneIndex;
            ik[i].targetBone = from.targetBone;
            ik[i].iterations = from.iterations;
            ik[i].maxAngle = from.maxAngle;
            ik[i].linkCount = std::uint8_t(links[i].size());
            ik[i].links = links[i].data();
        }
        for (unsigned i = 0; i < morphs.size(); ++i) {
            const auto& from = d.morphs[i];
            auto& to = morphs[i];
            std::memcpy(to.name, from.header.name, sizeof to.name);
            if (i)
                std::memcpy(to.nameEn, d.englishMorphNames[i - 1].data(), sizeof to.nameEn);
            to.panel = mdl::MorphPanel(from.header.panel);
            for (const auto& entry : from.offsets) {
                mdl::PmdVertexMorphEntry offset{};
                offset.vertexIndex =
                    i ? d.morphs[0].offsets[entry.vertexIndex].vertexIndex : entry.vertexIndex;
                std::memcpy(offset.offset, entry.offset, sizeof offset.offset);
                offsets[i].push_back(offset);
            }
            to.offsetCount = int(offsets[i].size());
            to.vertexEntries = offsets[i].data();
        }
        const unsigned firstGroup = d.facialTargets.empty() ? 2 : 3;
        for (unsigned i = 0; i < d.groupNames.size(); ++i) {
            std::memcpy(groups[firstGroup + i].name, d.groupNames[i].data(), 50);
            std::memcpy(groups[firstGroup + i].nameEn, d.englishGroupNames[i].data(), 50);
        }
        for (unsigned i = 0; i < facial.size(); ++i)
            facial[i].targetIndex = d.facialTargets[i];
        for (unsigned i = 0; i < boneRows.size(); ++i) {
            boneRows[i].targetIndex = d.boneFrames[i].bone;
            boneRows[i].groupIndex = std::uint16_t(d.boneFrames[i].group + firstGroup - 1);
        }
        for (unsigned i = 0; i < rigids.size(); ++i) {
            const auto& from = d.rigids[i];
            auto& to = rigids[i];
            std::memcpy(to.name, from.pmdName, sizeof to.name);
            to.boneIndex = from.boneIndex;
            to.group = from.group;
            to.noCollapse = from.noCollapse;
            to.shape = from.shape;
            std::memcpy(to.size, from.size, sizeof to.size);
            std::memcpy(to.position, from.position, sizeof to.position);
            std::memcpy(to.rotation, from.rotation, sizeof to.rotation);
            to.mass = from.mass;
            to.linearDamping = from.linearDamping;
            to.angularDamping = from.angularDamping;
            to.restitution = from.restitution;
            to.friction = from.friction;
            to.mode = from.mode;
        }
        for (unsigned i = 0; i < joints.size(); ++i) {
            const auto& from = d.joints[i];
            auto& to = joints[i];
            std::memcpy(to.name, from.pmdName, sizeof to.name);
            to.rigidA = from.rigidA;
            to.rigidB = from.rigidB;
            std::memcpy(to.position, from.position, sizeof to.position);
            std::memcpy(to.rotation, from.rotation, sizeof to.rotation);
            std::memcpy(to.limits, from.limits, sizeof to.limits);
            std::memcpy(to.springs, from.springs, sizeof to.springs);
        }
        for (unsigned i = 0; i < 10; ++i)
            std::memcpy(model->pmdToonFileNames[i], d.toonNames[i].data(), 100);
        model->vertexCount = unsigned(vertices.size());
        model->rawVertices = vertices.data();
        model->indexCount = unsigned(indices.size());
        model->indices = indices.data();
        model->materialCount = unsigned(materials.size());
        model->materials = materials.data();
        model->boneCount = unsigned(bones.size());
        model->boneTable = bones.data();
        model->ikChainCount = unsigned(ik.size());
        model->ikChains = ik.data();
        model->morphCount = unsigned(morphs.size());
        model->morphs = morphs.data();
        model->morph0Count = offsets.empty() ? 0 : unsigned(offsets[0].size());
        model->morph0Table = offsets.empty() ? nullptr : offsets[0].data();
        model->groupCount = std::uint8_t(groups.size());
        model->displayGroups = groups.data();
        model->facialFrameCount = std::uint8_t(facial.size());
        model->displayFrames = facial.data();
        model->displayBoneCount = unsigned(boneRows.size());
        model->displayBones = boneRows.data();
        model->rigidCount = unsigned(rigids.size());
        model->rigidTable = rigids.data();
        model->jointCount = unsigned(joints.size());
        model->jointTable = joints.data();
    }
};
unsigned EnhancedSaveCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    auto open = &EnhancedOpen;
    auto write = &EnhancedWrite;
    auto close = &EnhancedClose;
    auto select = &EnhancedSelect;
    auto directory = &IgnoreCurrentDirectory;
    auto menu = +[](HWND) -> HMENU { return reinterpret_cast<HMENU>(1); };
    auto state = +[](HMENU, UINT, UINT) -> UINT { return 0; };
    auto message = &AboutMessage;
    Patch a(original.Function<void*>(0x128500), &open, sizeof open);
    Patch b(original.Function<void*>(0x128420), &write, sizeof write);
    Patch c(original.Function<void*>(0x1284d0), &close, sizeof close);
    Patch d(original.Function<void*>(0x128098), &select, sizeof select);
    Patch e(original.Function<void*>(0x128278), &directory, sizeof directory);
    Patch f(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch g(original.Function<void*>(0x128660), &state, sizeof state);
    Patch h(original.Function<void*>(0x128618), &message, sizeof message);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    enhancedNativePath = (root / "native-enhanced.pmd").wstring();
    enhancedTextureWriteReturn = original.Function<void*>(0xb600e);
    unsigned commands = 0;
    for (unsigned profile = 0; profile < 4; ++profile)
        for (bool accepted : {false, true})
            for (bool error : {false, true}) {
                const auto source = EnhancedSaveFixture(profile);
                NativeEnhancedModel native(source);
                std::memset(app.get(), 0, sizeof(*app));
                app->modelSlots[0] = native.model.get();
                app->sceneModified = 0x7d;
                auto* raw = reinterpret_cast<unsigned char*>(app.get());
                raw[0xa1b90] = 1;
                raw[0xa1bb4] = 1;
                enhancedNativeBytes.clear();
                enhancedNativeOpens = enhancedNativeCloses = 0;
                enhancedNativeAccepted = accepted;
                enhancedNativeError = error;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr,
                                                                   MDS_CMD_SAVE_ENHANCED_MODEL);
                if (app->sceneModified != (accepted ? 1 : 0x7d) || raw[0xa1bb4] != !accepted)
                    std::fprintf(
                        stderr, "save case %u accepted=%u error=%u dirty=%u enhanced=%u opens=%u\n",
                        profile, unsigned(accepted), unsigned(error), unsigned(app->sceneModified),
                        unsigned(raw[0xa1bb4]), enhancedNativeOpens);
                Check(app->sceneModified == (accepted ? 1 : 0x7d) && raw[0xa1bb4] == !accepted,
                      "native enhanced flag vs scene modified flag");
                Check(enhancedNativeOpens == unsigned(accepted) &&
                          enhancedNativeCloses == unsigned(accepted && !error),
                      "native save IO lifetime");
                if (accepted && !error) {
                    std::vector<std::uint8_t> portable;
                    std::string reason;
                    Check(WriteEnhancedPmd(source, portable, reason), reason.c_str());
                    Check(EnhancedSaveHash(enhancedNativeBytes) ==
                              EnhancedNativeSaveHashes[profile],
                          "frozen original enhanced PMD stream");
                    if (enhancedNativeBytes != portable) {
                        const auto mismatch =
                            std::mismatch(enhancedNativeBytes.begin(), enhancedNativeBytes.end(),
                                          portable.begin(), portable.end());
                        std::fprintf(stderr, "PMD profile %u: native=%zu portable=%zu offset=%zu\n",
                                     profile, enhancedNativeBytes.size(), portable.size(),
                                     std::size_t(mismatch.first - enhancedNativeBytes.begin()));
                        throw std::runtime_error("native enhanced PMD bytes");
                    }
                    std::printf("Original PMD profile %u hash %016llx\n", profile,
                                static_cast<unsigned long long>(EnhancedSaveHash(portable)));
                    Check(std::wstring(native.model->path) == enhancedNativePath,
                          "native saved model path");
                } else
                    Check(enhancedNativeBytes.empty() && native.model->path[0] == 0,
                          "native cancelled/failed save retains path");
                ++commands;
            }
    return commands;
}
}
