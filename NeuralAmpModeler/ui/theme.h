// theme.h — TONE3000 Player design tokens.
// Every color, type size, spacing value, and radius used by ui/ pulls from here.
// Single source of truth. Change a constant → propagates everywhere.

#pragma once

#include "IGraphicsStructs.h"  // for iplug::igraphics::IColor

namespace t3k::theme {

using ::iplug::igraphics::IColor;

// ─── Colors (mirror of tone3000.com palette) ───────────────────────────────
inline const IColor kBgBase       {255,   0,   0,   0};   // pure black
inline const IColor kBgSurface    {255,   5,   5,   5};   // cards / elevated surfaces
inline const IColor kBgElevated   {255,  10,  10,  10};   // hovered / pressed
inline const IColor kBorder       {255,  22,  22,  22};   // 1px borders
inline const IColor kBorderActive {255, 255, 255,   0};   // focus / selection outline (yellow)
inline const IColor kAccent       {255, 255, 255,   0};   // #FFFF00 — primary CTA
inline const IColor kText         {255, 255, 255, 255};
inline const IColor kTextMuted    {255, 136, 136, 136};
inline const IColor kTextDim      {255,  85,  85,  85};

// Status colors (Phase 2b).
inline const IColor kError        {255, 255,  58,  58};   // #ff3a3a — destructive (hover-X on T3kSlot)
inline const IColor kWarning      {255, 255, 184,  77};   // #ffb84d — unsaved preset (T3kPresetPill dot)

// Signature rainbow (used on the audio scrubber + select accents)
inline const IColor kRainbowR     {255, 255,  45,  45};
inline const IColor kRainbowY     {255, 255, 212,   0};
inline const IColor kRainbowB     {255,  26,  26, 255};

// (No wordmark color tokens — the official TONE3000 logo SVG carries its own
//  colors and is rendered by T3kLogo via LoadSVG. See Task 16.)

// ─── Typography ────────────────────────────────────────────────────────────
// Font names — register with IGraphics::LoadFont in NeuralAmpModeler::OnUIOpen.
constexpr const char* kFontDisplay  = "Anton-Regular";
constexpr const char* kFontBody     = "Inter-Regular";
constexpr const char* kFontBodyMed  = "Inter-Medium";
constexpr const char* kFontBodySemi = "Inter-SemiBold";
constexpr const char* kFontBodyBold = "Inter-Bold";

// Sizes in logical pixels (scale automatically with iPlug2's DPI scaling).
constexpr float kTypeH1    = 31.f;  // section headings (Anton)
constexpr float kTypeH2    = 21.f;  // sub-headings
constexpr float kTypeBody  = 17.f;  // default body
constexpr float kTypeSmall = 14.f;  // stats, captions
constexpr float kTypeLabel = 13.f;  // knob labels, tab labels

// ─── Spacing (4-pt grid) ───────────────────────────────────────────────────
constexpr float kS1 = 5.f, kS2 = 10.f, kS3 = 16.f, kS4 = 21.f, kS5 = 31.f, kS6 = 42.f;

// ─── Radii ─────────────────────────────────────────────────────────────────
constexpr float kRadiusSm   = 5.f;
constexpr float kRadiusMd   = 10.f;
constexpr float kRadiusLg   = 16.f;
// kRadiusPill is a SENTINEL — do NOT pass it directly to FillRoundRect /
// DrawRoundRect. iPlug2's PathRoundRect builds its corners from four
// PathArc calls with NO radius clamp, and NanoVG's nvgArc happily draws
// 999-pixel arcs that span the entire viewport (this is the root cause
// of the "diagonal lines on the search bar / cards" Phase 6 smoke-test
// bug). For pill-shaped controls, call pillRadius(mRECT.H()) — it
// returns the largest geometrically valid corner radius for a true
// pill end.
constexpr float kRadiusPill = 999.f;
inline float pillRadius(float height) { return height * 0.5f; }

// ─── Animation durations (ms) ──────────────────────────────────────────────
constexpr int kAnimTabSlide       = 200;
constexpr int kAnimAccordionChevron = 150;
constexpr int kAnimCardHover      = 120;

// ─── Window dimensions ─────────────────────────────────────────────────────
// 2026-05-25 — design canvas trimmed further to 1024x640. Window at
// default scale (1.4) = 1434x896. Mirror of PLUG_* in config.h.
constexpr int kWindowMinW = 716;
constexpr int kWindowMinH = 448;
constexpr int kWindowMaxW = 2048;
constexpr int kWindowMaxH = 1280;
constexpr int kWindowDefW = 1024;
constexpr int kWindowDefH = 640;

}  // namespace t3k::theme
