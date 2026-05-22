// layout.h — small flexbox-style helpers atop iPlug2's IRECT.
// All public functions return std::vector<IRECT> sized to the input weights.
// No allocation in the audio thread, but these are only ever called on the
// GUI thread (OnResize/Draw) so vector allocation is fine.

#pragma once

#include "IGraphicsStructs.h"  // IRECT
#include <vector>
#include <utility>
#include <algorithm>

namespace t3k::layout {

using ::iplug::igraphics::IRECT;

// Split `rect` horizontally into pieces sized proportionally to `weights`.
// Optional `gap` (logical pixels) is inserted between pieces.
// Returns one IRECT per weight.
//
// Example:
//   auto cols = row(bounds, {1.f, 2.f, 1.f}, 8.f);
//   // cols[0] = left quarter, cols[1] = middle half, cols[2] = right quarter,
//   // with 8px gaps between them.
inline std::vector<IRECT> row(IRECT rect,
                              const std::vector<float>& weights,
                              float gap = 0.f)
{
  std::vector<IRECT> out;
  if (weights.empty()) return out;
  out.reserve(weights.size());

  float totalGap = gap * (weights.size() - 1);
  float availableW = rect.W() - totalGap;
  float totalWeight = 0.f;
  for (float w : weights) totalWeight += w;
  if (totalWeight <= 0.f) totalWeight = 1.f;

  float x = rect.L;
  for (float w : weights) {
    float pieceW = availableW * (w / totalWeight);
    out.emplace_back(x, rect.T, x + pieceW, rect.B);
    x += pieceW + gap;
  }
  return out;
}

// Vertical analogue of `row`. Split top-to-bottom.
inline std::vector<IRECT> col(IRECT rect,
                              const std::vector<float>& weights,
                              float gap = 0.f)
{
  std::vector<IRECT> out;
  if (weights.empty()) return out;
  out.reserve(weights.size());

  float totalGap = gap * (weights.size() - 1);
  float availableH = rect.H() - totalGap;
  float totalWeight = 0.f;
  for (float w : weights) totalWeight += w;
  if (totalWeight <= 0.f) totalWeight = 1.f;

  float y = rect.T;
  for (float w : weights) {
    float pieceH = availableH * (w / totalWeight);
    out.emplace_back(rect.L, y, rect.R, y + pieceH);
    y += pieceH + gap;
  }
  return out;
}

// Split a rect into a top strip of fixed height and a remainder.
// Returns {top_strip, remainder}.
inline std::pair<IRECT, IRECT> rowFixedTop(IRECT rect, float topH)
{
  topH = std::min(topH, rect.H());
  return { IRECT(rect.L, rect.T, rect.R, rect.T + topH),
           IRECT(rect.L, rect.T + topH, rect.R, rect.B) };
}

// Bottom strip + remainder. Returns {remainder, bottom_strip}.
inline std::pair<IRECT, IRECT> rowFixedBottom(IRECT rect, float bottomH)
{
  bottomH = std::min(bottomH, rect.H());
  return { IRECT(rect.L, rect.T, rect.R, rect.B - bottomH),
           IRECT(rect.L, rect.B - bottomH, rect.R, rect.B) };
}

// Inset a rect by the same amount on all sides.
inline IRECT pad(IRECT rect, float p) { return rect.GetPadded(-p); }

// Inset a rect by individual sides.
inline IRECT pad(IRECT rect, float top, float right, float bottom, float left)
{
  return IRECT(rect.L + left, rect.T + top, rect.R - right, rect.B - bottom);
}

}  // namespace t3k::layout
