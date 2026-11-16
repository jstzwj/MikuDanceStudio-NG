#pragma once
#include <QLibrary>
#include <QCoreApplication>
#include <QDir>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace mikudancestudio::qt {
// Do not bind unversioned system FFmpeg accidentally. Check all four ABI majors
// before dereferencing public structs. Qt's deployment supplies these libraries.
struct MediaRuntime {
    QLibrary util, codec, format, scale;
    std::string error;
#define MDS_MEDIA_SYMBOLS(X)                                                                       \
    X(avformat_version)                                                                            \
    X(avformat_alloc_context) X(avformat_open_input) X(avformat_find_stream_info)                  \
        X(av_find_best_stream) X(avformat_close_input) X(av_read_frame) X(av_seek_frame)           \
            X(av_guess_sample_aspect_ratio) X(av_packet_side_data_get) X(avcodec_version)          \
                X(avcodec_alloc_context3) X(avcodec_parameters_to_context) X(avcodec_open2)        \
                    X(avcodec_free_context) X(avcodec_flush_buffers) X(avcodec_send_packet)        \
                        X(avcodec_receive_frame) X(av_packet_alloc) X(av_packet_free)              \
                            X(av_packet_unref) X(avutil_version) X(av_frame_alloc)                 \
                                X(av_frame_free) X(av_frame_unref) X(av_rescale_q)                 \
                                    X(av_rescale_q_rnd) X(av_display_rotation_get) X(av_dict_set)  \
                                        X(av_dict_free) X(swscale_version) X(sws_getCachedContext) \
                                            X(sws_freeContext) X(sws_scale) X(sws_getCoefficients) \
                                                X(sws_setColorspaceDetails)
#define MDS_MEDIA_DECLARE(name) decltype(&::name) name = nullptr;
    MDS_MEDIA_SYMBOLS(MDS_MEDIA_DECLARE)
#undef MDS_MEDIA_DECLARE
    MediaRuntime() {
        auto load = [&](QLibrary& library, const char* base, unsigned major) {
#if defined(Q_OS_WIN)
            const auto name = QString("%1-%2").arg(base).arg(major);
#elif defined(Q_OS_MACOS)
            const auto name = QString("lib%1.%2.dylib").arg(base).arg(major);
#elif defined(Q_OS_ANDROID)
            const auto name = QString("lib%1.so").arg(base);
#else
            const auto name = QString("lib%1.so.%2").arg(base).arg(major);
#endif
            const auto app = QCoreApplication::applicationDirPath();
            for (const auto& dir :
                 {app, app + "/lib", app + "/../lib", app + "/../Frameworks", QString{}}) {
                library.setFileName(dir.isEmpty() ? name : QDir(dir).filePath(name));
                if (library.load())
                    return true;
            }
            error = "background-video-runtime-missing:" + name.toStdString();
            return false;
        };
        if (!load(util, "avutil", 59) || !load(codec, "avcodec", 61) ||
            !load(format, "avformat", 61) || !load(scale, "swscale", 8))
            return;
#define MDS_MEDIA_RESOLVE(name)                                                                    \
    name = reinterpret_cast<decltype(name)>(util.resolve(#name));                                  \
    if (!name)                                                                                     \
        name = reinterpret_cast<decltype(name)>(codec.resolve(#name));                             \
    if (!name)                                                                                     \
        name = reinterpret_cast<decltype(name)>(format.resolve(#name));                            \
    if (!name)                                                                                     \
        name = reinterpret_cast<decltype(name)>(scale.resolve(#name));                             \
    if (!name) {                                                                                   \
        error = "background-video-runtime-symbol:" #name;                                          \
        return;                                                                                    \
    }
        MDS_MEDIA_SYMBOLS(MDS_MEDIA_RESOLVE)
#undef MDS_MEDIA_RESOLVE
        if ((avutil_version() >> 16) != 59 || (avcodec_version() >> 16) != 61 ||
            (avformat_version() >> 16) != 61 || (swscale_version() >> 16) != 8)
            error = "background-video-runtime-abi-unsupported";
    }
#undef MDS_MEDIA_SYMBOLS
};
inline MediaRuntime& VideoRuntime() {
    static MediaRuntime runtime;
    return runtime;
}
}
