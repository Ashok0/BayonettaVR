#pragma once

namespace bvr {
struct Config {
    bool enabled = true;
    bool nativeStereo = true;
    bool forceHalfRate = false;
    bool uncapPresent = true;
    bool positionalTracking = true;
    bool compositorRotation = true;
    bool automaticTurning = false;
    bool directPoseTurning = true;
    float compositorTurnDegrees = 12.0f;
    bool applyToUi = false;
    bool swapEyes = false;
    bool mirrorSideBySide = false;
    bool compositorPoseDiagnostic = false;
    float worldUnitsPerMetre = 1.0f;
    float stereoScale = 1.0f;
    float gameVerticalFovDegrees = 60.0f;
    float nearClip = 0.1f;
    float farClip = 100000.0f;
    int projectionMode = 0;
    int matrixRegisterOverride = -1;
};

const Config& Cfg();
void LoadConfig();
}
