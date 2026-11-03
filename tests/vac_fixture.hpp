#pragma once
#include <string>
#include <vector>

inline std::vector<std::string> VacCases() {
    std::vector<std::string> result;
    for (const char* scale : {"1.0", "0", "-1.75", "+4.4e1", "0.000125", "0.1"})
        for (const char* shadow : {"0", "1", "2", "-1", "255", "256", "", "// old VAC"})
            for (bool crlf : {false, true}) {
                std::string text = std::string("ignored title\nmesh\\bound.x\n") + scale +
                                   "\n 0.6,-1.9, -0.3\n0.0, -0.0, 3.14\nignored bone\n" + shadow +
                                   "\n";
                if (crlf) {
                    std::string windows;
                    for (char c : text) {
                        if (c == '\n')
                            windows += '\r';
                        windows += c;
                    }
                    text = std::move(windows);
                }
                result.push_back(std::move(text));
            }
    return result;
}
