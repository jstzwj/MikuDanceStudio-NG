#pragma once
#include <algorithm>
#include <string_view>

namespace mikudancestudio::editor {
enum class DroppedFileKind {
    Unknown,
    Project,
    Model,
    Pose,
    Motion,
    Accessory,
    Video,
    Bitmap,
    Jpeg,
    Wave,
    Vocaloid,
    Kinect
};

// The original dispatcher searches the complete UTF-16 path in this order.
// Preserve its explicit case variants, substring matches and dotless "Vac".
// DragQueryFileW supplies at most 511 code units plus the terminating zero.
inline std::u16string_view DroppedFilePath(std::u16string_view path) {
    return path.substr(0, (std::min)(std::size_t(511), path.find(u'\0')));
}
inline DroppedFileKind ClassifyDroppedFile(std::u16string_view path) {
    path = DroppedFilePath(path);
    const auto has = [&](auto a, auto b, auto c) {
        return path.find(a) != path.npos || path.find(b) != path.npos || path.find(c) != path.npos;
    };
    if (has(u".pmm", u".PMM", u".Pmm"))
        return DroppedFileKind::Project;
    if (has(u".pmd", u".PMD", u".Pmd") || has(u".pmx", u".PMX", u".Pmx"))
        return DroppedFileKind::Model;
    if (has(u".vpd", u".VPD", u".Vpd"))
        return DroppedFileKind::Pose;
    if (has(u".vmd", u".VMD", u".Vmd"))
        return DroppedFileKind::Motion;
    if (has(u".x", u".X", u".x") || has(u".vac", u".VAC", u"Vac"))
        return DroppedFileKind::Accessory;
    if (has(u".avi", u".AVI", u".Avi"))
        return DroppedFileKind::Video;
    if (has(u".bmp", u".BMP", u".Bmp"))
        return DroppedFileKind::Bitmap;
    if (has(u".jpg", u".JPG", u".Jpg"))
        return DroppedFileKind::Jpeg;
    if (has(u".wav", u".WAV", u".Wav"))
        return DroppedFileKind::Wave;
    if (has(u".vsq", u".VSQ", u".Vsq"))
        return DroppedFileKind::Vocaloid;
    if (has(u".oni", u".ONI", u".Oni"))
        return DroppedFileKind::Kinect;
    return DroppedFileKind::Unknown;
}
}
