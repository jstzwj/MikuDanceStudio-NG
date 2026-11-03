#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmm_morph_registration.hpp"
#include "mikudancestudio/pmm_key_selection.hpp"
#include "mikudancestudio/vsq_format.hpp"
#include <cmath>
#include <fstream>

namespace mikudancestudio::editor {
bool EditorSession::ImportVsq(const std::filesystem::path& path, std::string& reason,
                              unsigned* failedKeys) {
    if (playing_ || selection_.model == 255) {
        reason = "vsq-needs-model";
        return false;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 64u * 1024 * 1024) {
        reason = "vsq-open-error";
        return false;
    }
    std::vector<std::uint8_t> bytes(std::size_t(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) {
        reason = "vsq-open-error";
        return false;
    }
    format::VsqDocument vsq;
    if (!format::ReadVsq(bytes, vsq, reason))
        return false;
    // Native LoadVsqFile replaces the first whole-path .vsq occurrence and
    // writes its analysis before checking the model's four required mouths.
    auto textPath = path.u16string();
    const auto extension = textPath.find(u".vsq");
    if (extension != std::u16string::npos)
        textPath.replace(extension + 1, 3, u"txt");
    else {
        reason = "vsq-path-unsupported";
        return false;
    }
    std::ofstream text(std::filesystem::path(textPath), std::ios::binary | std::ios::trunc);
    if (!text || !text.write(vsq.report.data(), std::streamsize(vsq.report.size())) ||
        !text.flush()) {
        reason = "vsq-report-write-error";
        return false;
    }
    auto next = Current();
    auto model = std::find_if(next.document.models.begin(), next.document.models.end(),
                              [&](const auto& value) { return value.slot == selection_.model; });
    if (model == next.document.models.end()) {
        reason = "vsq-needs-model";
        return false;
    }
    unsigned mouths[4];
    const unsigned second[]{0xa0, 0xa2, 0xa4, 0xa8};
    for (unsigned i = 0; i < 4; ++i) {
        const auto found =
            std::find_if(model->morphNames.begin(), model->morphNames.end(), [&](const auto& name) {
                return name.size() == 2 && std::uint8_t(name[0]) == 0x82 &&
                       std::uint8_t(name[1]) == second[i];
            });
        if (found == model->morphNames.end()) {
            // The native menu marks the scene modified after returning from
            // analysis, even when a required mouth is missing. Retain all
            // document/CPU/history state while reflecting that command result.
            revision_ = nextRevision_++;
            reason = std::string("vsq-missing-") + "aiuo"[i];
            return false;
        }
        mouths[i] = unsigned(found - model->morphNames.begin());
    }
    if (!format::SetPmmSelectedKeys(next.document, selection_.model, selection_.keys)) {
        reason = "vsq-invalid-selection";
        return false;
    }
    const auto clear = [](auto& track) {
        for (auto& key : track.dense)
            key.allocated = 0;
        for (auto& key : track.sparse)
            key.value.allocated = 0;
        for (auto& item : track.unused)
            item.second.allocated = 0;
    };
    clear(model->boneKeys);
    clear(model->morphKeys);
    clear(model->displayKeys);
    format::PmmMorphRegistration pool;
    if (!pool.Load(model->morphKeys)) {
        reason = "vsq-invalid-track";
        return false;
    }
    unsigned failed = 0;
    const auto frame = [&](float seconds, unsigned& result) {
        const float value = seconds * 30.f;
        if (!std::isfinite(value) || value < 2 || value >= 2147483648.f) {
            reason = "vsq-frame-not-representable";
            return false;
        }
        result = unsigned(value);
        return true;
    };
    const auto key = [&](unsigned mouth, unsigned at, float weight) {
        model->morphPose[mouth] = weight;
        if (!pool.Register(unsigned(model->morphKeys.dense.size()), mouth, at, weight,
                           model->selection.maxFrame))
            ++failed;
    };
    for (const auto& event : vsq.events) {
        unsigned start, end;
        if (!frame(event.start, start) || !frame(event.end, end))
            return false;
        const auto vowel =
            event.vowel == 'h' && !event.consonant.empty() ? event.consonant.front() : event.vowel;
        unsigned mouth = 0;
        float weight = 0;
        switch (vowel) {
        case 'a':
            mouth = mouths[0];
            weight = .7f;
            break;
        case 'i':
            mouth = mouths[1];
            weight = .6f;
            break;
        case 'u':
            mouth = mouths[2];
            weight = 1.f;
            break;
        case 'o':
            mouth = mouths[3];
            weight = 1.f;
            break;
        case 'e':
            for (unsigned i = 0; i < 4; ++i) {
                const auto at = i == 0 ? start - 2 : i == 1 ? start : i == 2 ? end - 2 : end;
                key(mouths[0], at, i == 1 || i == 2 ? .5f : 0.f);
                key(mouths[1], at, i == 1 || i == 2 ? .3f : 0.f);
            }
            continue;
        default:
            continue;
        }
        key(mouth, start - 2, 0);
        key(mouth, start, weight);
        key(mouth, end - 2, weight);
        key(mouth, end, 0);
    }
    pool.Store(model->morphKeys);
    unsigned last;
    if (!frame(vsq.events.back().end, last))
        return false;
    next.document.timeline.currentFrame = int(last);
    next.document.timeline.startFrame = last > 6 ? int(last - 6) : 0;
    next.document.timeline.lastRegisteredFrame =
        std::max(next.document.timeline.lastRegisteredFrame, int(model->selection.maxFrame));
    // Only the selected model is sought; unrelated current poses and global
    // camera/light/accessory state survive the command tail.
    format::SceneSession evaluated;
    if (!format::SceneSession::Create(next.document, evaluated, reason) || !evaluated.Seek(last))
        return false;
    for (const auto& value : evaluated.Document().models)
        if (value.slot == selection_.model)
            *model = value;
    if (!format::ReadPmmSelectedKeys(next.document, selection_.model, next.selection.keys)) {
        reason = "vsq-invalid-selection";
        return false;
    }
    HistoryBatch history;
    for (const auto& value : next.document.models)
        history.emplace(value.slot, CaptureHistory(value, HistoryKind::None, 0));
    EndGesture();
    if (!Commit(std::move(next), reason, history, false))
        return false;
    if (failedKeys)
        *failedKeys = failed;
    return true;
}
}
