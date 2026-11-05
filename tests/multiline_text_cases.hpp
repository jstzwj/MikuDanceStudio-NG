#pragma once
#include <QString>
#include <array>

inline std::array<QString, 5> MultilineUnicodeCases() {
    return {
        QString::fromUtf8("初音ミク 日本語\nかな カナ 漢字"),
        QString::fromUtf8("初音未来 中文\n简体中文 多行备注"),
        QString::fromUtf8("caf\xc3\xa9 e\xcc\x81 na\xc3\xafve\nA\xcc\x8a office ffi"),
        QString::fromUtf8(
            "Miku \xf0\x9f\x8e\xb5 \xf0\x9f\x8c\x9f\n\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb music"),
        QString::fromUtf8(
            "English \xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9 123\nabc \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d xyz")};
}
