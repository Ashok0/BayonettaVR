#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace bvr {
// -1: no named projection (try instruction analysis).
// -2: only an explicitly named light/shadow projection; do NOT fall back to
// dp4 matching and accidentally turn the light's camera into the HMD camera.
inline int NamedSceneCameraMatrix(const std::string& text) {
    bool lightProjection = false;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? end : end - start);
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const bool projection = line.find("worldviewproj") != std::string::npos ||
            line.find("modelviewproj") != std::string::npos ||
            line.find("viewprojection") != std::string::npos ||
            line.find("viewprojmatrix") != std::string::npos;
        if (projection) {
            const bool light = line.find("shadow") != std::string::npos ||
                               line.find("light") != std::string::npos;
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                if (line[i] != 'c' || !std::isdigit(static_cast<unsigned char>(line[i + 1]))) continue;
                if (!light) return std::atoi(line.c_str() + i + 1);
                lightProjection = true;
                break;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lightProjection ? -2 : -1;
}
}
