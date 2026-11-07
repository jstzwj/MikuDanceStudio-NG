#pragma once
#include <map>
#include <string>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <climits>
// Shared original SAS algorithms: name matching and subset membership.
namespace mme_rules {
inline bool WildcardMatch(const char* name, size_t nameLen,
                             const char* pat, size_t patLen, bool ignoreCase)
{
    const char* np = name;
    const char* const nend = name + nameLen;
    const char* pp = pat;
    const char* const pend = pat + patLen;
    if (pp == pend) {
        return np == nend;
    }
    char pc = *pp;
    while (true) {
        pc = *pp;
        if (np == nend) {
            break;
        }
        if (pc == '*') {
            goto star;
        }
        if (pc == '?') {
            ++pp;
            ++np;
        } else {
            if (pc == '\\' && ++pp == pend) {
                return false;
            }
            if (ignoreCase) {
                if (tolower(static_cast<unsigned char>(*np)) !=
                    tolower(static_cast<unsigned char>(*pp))) {
                    return false;
                }
            } else if (*np != *pp) {
                return false;
            }
            ++np;
            ++pp;
        }
        if (pp == pend) {
            return np == nend;
        }
    }
    if (pc != '*') {
        return false;
    }
star:
    do {
        ++pp;
    } while (pp != pend && *pp == '*');
    if (pp == pend) {
        return true;
    }
    char first = *pp;
    if (ignoreCase) {
        first = static_cast<char>(
            tolower(static_cast<unsigned char>(first)));
    }
    const size_t tailLen = static_cast<size_t>(pend - pp);
    while (true) {
        if (first != '?' && first != '\\') {
            for (; np < nend; ++np) {
                if (first == *np) {
                    break;
                }
                if (ignoreCase &&
                    first ==
                        static_cast<char>(
                            tolower(static_cast<unsigned char>(*np)))) {
                    break;
                }
            }
        }
        if (WildcardMatch(np, static_cast<size_t>(nend - np), pp, tailLen,
                             ignoreCase)) {
            return true;
        }
        if (np == nend) {
            return false;
        }
        ++np;
    }
}

inline bool SubsetRanges(std::map<int,int>& ranges, const std::string& value) {
    ranges.clear();
    const char* p = value.c_str();
    long start = -1;
    while (*p != '\0' || start >= 0) {
        if (isspace(static_cast<unsigned char>(*p))) {
            ++p;
            continue;
        }
        if (*p == '-') {
            break;  // leading/misplaced '-' -> parse error
        }
        char* end = nullptr;
        errno = 0;
        long v = strtol(p, &end, 0);
        if (v < 0 || v > INT_MAX || errno == ERANGE) {
            break;  // negative subset index -> parse error
        }
        if (end == p) {
            // No digits consumed: only valid when it closes an open range
            // ("N-" directly followed by ',' or the end of the string).
            if (start < 0 || (*end != ',' && *end != '\0')) {
                break;
            }
            ranges[static_cast<int>(start)] = 0x7fffffff;
            start = -1;
            if (*end == '\0') {
                break;
            }
            p = end + 1;  // past ','
            continue;
        }
        for (; isspace(static_cast<unsigned char>(*end)); ++end) {
        }
        p = end;
        if (*end == ',' || *end == '\0') {
            if (start >= 0) {
                ranges[static_cast<int>(start)] = static_cast<int>(v);
                start = -1;
            } else {
                ranges[static_cast<int>(v)] = static_cast<int>(v);
            }
            if (*end == '\0') {
                break;
            }
            ++p;  // past ','
            continue;
        }
        if (*end != '-' || start >= 0) {
            break;  // trailing garbage / second '-' -> parse error
        }
        start = v;
        ++p;  // past '-'
    }
    if (*p != '\0') {
        return false;  // stopped on an unparsable character
    }
    if (ranges.empty()) {
        // [sub_1800169D0 0x18001774a-0x18001776b] a fully-consumed spec with
        // NO ranges (e.g. Subset="") inserts the (0x7fffffff, 0x7fffffff)
        // sentinel pair. The selector [sub_18001DB50 0x18001dc11-0x18001dc3e]
        // walks the ranges as a RESTRICTION: the sentinel's min (0x7fffffff)
        // exceeds every real subset index, so the technique matches NO subset
        // ("hidden" - not "no restriction"; allSubsets stays false).
        ranges[0x7fffffff] = 0x7fffffff;
    }
    return true;
}

}
