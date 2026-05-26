#include <algorithm> // std::clamp, std::min
#include <cctype>    // std::tolower (used by StageModelInSlot extension check)
#include <cmath> // pow
#include <cstring>   // std::strlen
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

#if defined(_WIN32)
  // For StripResizeChrome below. The host's floating wrapper window is
  // a top-level Win32 HWND; we mutate its style to disable the OS
  // resize chrome (edges/corners + Aero Snap maximize) so only our
  // own bottom-right ICornerResizerControl drives resizing.
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

#include "nlohmann/json.hpp"

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "NeuralAmpModeler.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "NeuralAmpModelerControls.h"
#include "cloud/LibrarySync.h"
#include "cloud/Session.h"
#include "crash/Minidump.h"
#include "settings/Settings.h"
#include "ui/ToneRoot.h"
#include "ui/theme.h"

using namespace iplug;
using namespace igraphics;

namespace {

#if defined(_WIN32)
// 2026-05-25 — locks down the host's floating plugin-wrapper window so
// only our plugin-internal bottom-right corner resizer can resize the
// editor. We strip:
//   * WS_THICKFRAME   — kills the OS-level resize cursors that show up
//                       on every edge / non-BR corner of the wrapper.
//                       SetWindowPos still works for our own
//                       plugFrame->resizeView calls (style only
//                       gates *user* interaction).
//   * WS_MAXIMIZEBOX  — disables Aero Snap's drag-to-top maximize on
//                       Windows 11 (and the maximize button if any).
//
// We only mutate the wrapper if it's a true top-level window AND it
// currently has resize chrome. Hosts that embed the plugin inside a
// non-resizable pane (REAPER's FX dialog, etc.) lack these styles, so
// the call becomes a no-op and we leave the host alone.
void StripResizeChrome(IGraphics* g)
{
  if (!g) return;
  HWND hwnd = static_cast<HWND>(g->GetWindow());
  if (!hwnd) return;
  HWND parent = ::GetParent(hwnd);  // host's wrapper
  if (!parent) return;
  // Only touch true top-level windows. If the wrapper itself has a
  // parent, it's embedded inside the host's UI and not the floating
  // window the user can snap-maximize.
  if (::GetParent(parent) != nullptr) return;

  LONG style = ::GetWindowLongW(parent, GWL_STYLE);
  const LONG kStripMask = WS_THICKFRAME | WS_MAXIMIZEBOX;
  if ((style & kStripMask) == 0) return;  // already stripped

  style &= ~kStripMask;
  ::SetWindowLongW(parent, GWL_STYLE, style);
  // Force the non-client area to repaint without the resize gripper
  // and maximize button. SWP_FRAMECHANGED tells Windows the style
  // changed so the WM_NCCALCSIZE machinery runs.
  ::SetWindowPos(parent, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOOWNERZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);
}
#else
inline void StripResizeChrome(IGraphics*) {}
#endif

int SafeMaxBlockSize(int hostBlockSize)
{
  return hostBlockSize > 0 ? hostBlockSize : 2048;
}

}  // namespace

const double kDCBlockerFrequency = 5.0;

// Styles
const IVColorSpec colorSpec{
  DEFAULT_BGCOLOR, // Background
  PluginColors::NAM_THEMECOLOR, // Foreground
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), // Pressed
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), // Frame
  PluginColors::MOUSEOVER, // Highlight
  DEFAULT_SHCOLOR, // Shadow
  PluginColors::NAM_THEMECOLOR, // Extra 1
  COLOR_RED, // Extra 2 --> color for clipping in meters
  PluginColors::NAM_THEMECOLOR.WithContrast(0.1f), // Extra 3
};

const IVStyle style =
  IVStyle{true, // Show label
          true, // Show value
          colorSpec,
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Middle, PluginColors::NAM_THEMEFONTCOLOR}, // Knob label text5
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Bottom, PluginColors::NAM_THEMEFONTCOLOR}, // Knob value text
          DEFAULT_HIDE_CURSOR,
          DEFAULT_DRAW_FRAME,
          false,
          DEFAULT_EMBOSS,
          0.2f,
          2.f,
          DEFAULT_SHADOW_OFFSET,
          DEFAULT_WIDGET_FRAC,
          DEFAULT_WIDGET_ANGLE};
const IVStyle titleStyle =
  DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular")).WithDrawFrame(false).WithShadowOffset(2.f);
const IVStyle radioButtonStyle =
  style
    .WithColor(EVColor::kON, PluginColors::NAM_THEMECOLOR) // Pressed buttons and their labels
    .WithColor(EVColor::kOFF, PluginColors::NAM_THEMECOLOR.WithOpacity(0.1f)) // Unpressed buttons
    .WithColor(EVColor::kX1, PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f)); // Unpressed buttons' labels

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;


NeuralAmpModeler::NeuralAmpModeler(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  t3k::crash::installCrashHandler();
  nam::activations::Activation::enable_fast_tanh();
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kDryWet)->InitDouble("Dry/Wet", 100.0, 0.0, 100.0, 0.1, "%");
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kIRToggle)->InitBool("IRToggle", true);
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  GetParam(kSlim)->InitDouble("Slim", 0.0, 0.0, 1.0, 0.01);
  GetParam(kMasterOutput)->InitGain("Master", 0.0, -40.0, 12.0, 0.1);

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    // Global IGraphics config.
    pGraphics->AttachTextEntryControl();   // needed by T3kSearchBar
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    // 2026-05-25 — bottom-right corner resizer (re-enabled).
    // EUIResizerMode::Scale: dragging only changes the IGraphics draw
    // scale, leaving Width()/Height() (= the design canvas) fixed.
    // That matches the rest of our preset handling and keeps ToneRoot's
    // mRECT valid during the drag (see OnUIOpen for the rationale).
    // The previous attach was removed because COLOR_TRANSLUCENT (alpha
    // 10) left a faint triangle visible across the UI; COLOR_TRANSPARENT
    // (alpha 0) is fully invisible until mouse-over. The cursor still
    // flips to SIZENWSE so users discover the handle. PLUG_HOST_RESIZE
    // must be 1 for the resulting resizeView callback to be honored —
    // see config.h.
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale,
                                   /*layoutOnResize*/ false,
                                   /*idle      */ COLOR_TRANSPARENT,
                                   /*mouseOver */ COLOR_TRANSLUCENT,
                                   /*drag      */ COLOR_BLACK,
                                   /*size px   */ 20.f);

    // Pure-black panel background (TONE3000 brand).
    pGraphics->AttachPanelBackground(t3k::theme::kBgBase);

    // Fonts: upstream's Roboto/Michroma + Phase 2 Anton/Inter family.
    pGraphics->LoadFont("Roboto-Regular",   ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular", MICHROMA_FN);
    pGraphics->LoadFont("Anton-Regular",    ANTON_REGULAR_FN);
    pGraphics->LoadFont("Inter-Regular",    INTER_REGULAR_FN);
    pGraphics->LoadFont("Inter-Medium",     INTER_MEDIUM_FN);
    pGraphics->LoadFont("Inter-SemiBold",   INTER_SEMIBOLD_FN);
    pGraphics->LoadFont("Inter-Bold",       INTER_BOLD_FN);

    // The whole UI is one root that owns everything (header, tabs, body,
    // knob row, overlays). See ui/ToneRoot.{h,cpp}.
    auto* toneRoot = new t3k::ui::ToneRoot(pGraphics->GetBounds(), *this);
    pGraphics->AttachControl(toneRoot);

    // Global hotkeys (2026-05-25):
    //   Ctrl+Shift+0 — reset window size to the default (small).
    //                  Recovery path when the editor ends up off-screen
    //                  or unreadably small (host DPI mishap, stale
    //                  corner-drag scale, etc.). Matches the "RESET
    //                  WINDOW SIZE" button on the settings modal.
    //   Ctrl+Shift+S — open the Settings modal (same target as the
    //                  account menu's "Settings…" item).
    //
    // Both require three modifiers + a non-letter/digit so they stay
    // out of normal text-entry traffic. Reset preset must mirror
    // OnUIOpen() / T3kSettingsModal::PresetFor() (logical canvas =
    // design size, scale-only varies — see OnUIOpen for rationale).
    pGraphics->SetKeyHandlerFunc(
        [pGraphics, toneRoot](const IKeyPress& key, bool isUp) -> bool {
          if (isUp) return false;
          // IKeyPress.S/.C/.A are SHIFT/CTRL(CMD on Mac)/ALT.
          if (!key.C || !key.S) return false;

          // Ctrl+Shift+0 — VK_0 = 0x30. Reset window scale to the
          // default (1.35 over the 1024x640 design canvas =
          // ~1382x864 physical pixels). Matches the "RESET WINDOW
          // SIZE" button on the settings modal.
          if (key.VK == 0x30) {
            ::t3k::settings::instance().window_scale = 1.35f;
            ::t3k::settings::save();
            pGraphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, 1.35f,
                              /*needsPlatformResize*/ true);
            return true;
          }
          // Ctrl+Shift+S — VK_S = 0x53.
          if (key.VK == 0x53) {
            if (toneRoot) toneRoot->openSettings();
            return true;
          }
          return false;
        });
  };

  // Force-init the cloud Session singleton so its constructor (which
  // calls TokenStore::loadRefreshToken and kicks off the refresh
  // thread) runs BEFORE ToneRoot::OnAttached queries Session::state().
  // Without this, the avatar/sign-in pill would briefly render in the
  // SignedOut state on the first paint, then flicker when the lazy
  // ctor finally fires.
  (void)::t3k::cloud::Session::instance();

  // Phase 8 — start the library-sync client. When
  // SyncConfig::kLibrarySyncUrl is the REPLACE_ME placeholder, start()
  // is a complete no-op and the plug-in behaves identically to
  // Phase 7. Once the user deploys their Worker and updates the
  // constant, start() subscribes to Session + EventBus and runs an
  // initial pull on sign-in.
  ::t3k::cloud::sync::LibrarySync::instance().start();
}

NeuralAmpModeler::~NeuralAmpModeler()
{
  _DeallocateIOPointers();
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numChannelsInternal = kNumChannelsInternal;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _PrepareBuffers(numChannelsInternal, numFrames);
  // Input is collapsed to mono in preparation for the NAM.
  _ProcessInput(inputs, numFrames, numChannelsExternalIn, numChannelsInternal);
  _ApplyDSPStaging();
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 0.1; // Quadratic...
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }

  if (mModel != nullptr)
  {
    mModel->process(triggerOutput, mOutputPointers, nFrames);
  }
  else
  {
    _FallbackDSP(triggerOutput, mOutputPointers, numChannelsInternal, numFrames);
  }
  // Apply the noise gate after the NAM
  sample** gateGainOutput =
    noiseGateActive ? mNoiseGateGain.Process(mOutputPointers, numChannelsInternal, numFrames) : mOutputPointers;

  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(gateGainOutput, numChannelsInternal, nFrames)
                                    : gateGainOutput;

  // ── Phase 10: chain the extra slots in series. Each slot reads from
  // `currentPointers`, runs its own NAM model + tone stack with its
  // stored EQ, applies per-slot in/out gain, and produces its output
  // in mOutputPointers (which acts as the chain's rolling buffer).
  // To avoid in-place read/write hazards when the slot's input and
  // output both alias mOutputPointers, we copy through a thread-local
  // scratch buffer. ProcessBlock is called from a single audio thread
  // per instance, so thread_local is safe.
  static thread_local std::vector<sample> chainScratch;
  static thread_local std::vector<sample*> chainScratchPtr(1, nullptr);
  if (chainScratch.size() < numFrames)
    chainScratch.resize(numFrames);
  chainScratchPtr[0] = chainScratch.data();

  // Drain any pending chain-order update before walking the chain.
  // Lock-free: UI thread filled mPendingChainOrder + set dirty; we
  // atomically clear the flag and copy in. Skipped on common path
  // when no reorder happened.
  if (mChainOrderDirty.exchange(false, std::memory_order_acquire))
  {
    mChainOrderLen = mPendingChainOrderLen;
    for (int i = 0; i < mChainOrderLen; ++i)
      mChainOrder[i] = mPendingChainOrder[i];
  }

  sample** currentPointers = toneStackOutPointers;
  for (int orderIdx = 0; orderIdx < mChainOrderLen; ++orderIdx)
  {
    const int extraIdx = mChainOrder[orderIdx];
    if (extraIdx < 0 || extraIdx >= kNumExtraSlots) continue;
    auto& es = mExtraSlots[extraIdx];
    if (es.bypassed)
    {
      // Pass-through: input -> output unchanged. currentPointers keeps
      // pointing at the previous stage's output (the next iteration uses
      // it as its input), which is correct identity behaviour.
      continue;
    }
    // 2026-05-26 — slots can hold a NAM model OR an IR. Skip silent
    // (neither) slots; prefer model if both somehow coexist.
    if (es.model == nullptr && es.ir == nullptr)
      continue;
    // Apply per-slot input gain into scratch.
    const double inGain = DBToAmp(es.inGainDb);
    const double inStart = es.smoothedInGain;
    for (size_t s = 0; s < numFrames; ++s)
    {
      const double t = static_cast<double>(s + 1) / static_cast<double>(numFrames);
      const double g = inStart + (inGain - inStart) * t;
      chainScratch[s] = g * currentPointers[0][s];
    }
    es.smoothedInGain = inGain;

    sample** processed = nullptr;
    if (es.model != nullptr)
    {
      // Process the slot's NAM model — writes into mOutputPointers.
      es.model->process(chainScratchPtr.data(), mOutputPointers, (int)nFrames);
      processed = mOutputPointers;
    }
    else
    {
      // IR convolution — Process() returns its internal output buffer.
      processed = es.ir->Process(chainScratchPtr.data(), numChannelsInternal, numFrames);
    }
    // Slot's own tone stack (when its eqActive flag is set). For IR
    // slots the tone stack is still applied — useful as a post-IR EQ.
    sample** afterEq = (es.eqActive && es.toneStack)
                         ? es.toneStack->Process(processed, numChannelsInternal, nFrames)
                         : processed;
    // Apply slot's output gain back into mOutputArray. afterEq may
    // alias mOutputPointers (eqActive==false) or the tone stack's
    // internal buffer — copy back unconditionally so subsequent stages
    // see a stable pointer.
    const double outGain = DBToAmp(es.outGainDb);
    const double outStart = es.smoothedOutGain;
    // 2026-05-26 - apply per-slot output gain + dry/wet mix.
    // chainScratch[] holds the post-in-gain pre-process signal (the dry
    // reference); afterEq is the fully-processed signal.
    const double w = std::clamp(es.dryWet, 0.0, 1.0);
    const double wStart = es.smoothedDryWet;
    for (size_t s = 0; s < numFrames; ++s)
    {
      const double t = static_cast<double>(s + 1) / static_cast<double>(numFrames);
      const double og = outStart + (outGain - outStart) * t;
      const double mix = wStart + (w - wStart) * t;
      const double wet = afterEq[0][s];
      const double dry = chainScratch[s];
      mOutputArray[0][s] = (mix * og * wet) + ((1.0 - mix) * dry);
    }
    es.smoothedOutGain = outGain;
    es.smoothedDryWet = w;
    currentPointers = mOutputPointers;
  }

  sample** irPointers = currentPointers;
  if (mIR != nullptr && GetParam(kIRToggle)->Value())
    irPointers = mIR->Process(currentPointers, numChannelsInternal, numFrames);

  // And the HPF for DC offset (Issue 271)
  const double highPassCutoffFreq = kDCBlockerFrequency;
  // const double lowPassCutoffFreq = 20000.0;
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, highPassCutoffFreq);
  // const recursive_linear_filter::LowPassParams lowPassParams(sampleRate, lowPassCutoffFreq);
  mHighPass.SetParams(highPassParams);
  // mLowPass.SetParams(lowPassParams);
  sample** hpfPointers = mHighPass.Process(irPointers, numChannelsInternal, numFrames);
  // sample** lpfPointers = mLowPass.Process(hpfPointers, numChannelsInternal, numFrames);

  // 2026-05-26 - apply global MASTER output gain after the HPF.
  const double masterAmp = DBToAmp(GetParam(kMasterOutput)->Value());
  for (size_t c = 0; c < numChannelsInternal; ++c)
  {
    for (size_t s = 0; s < numFrames; ++s)
    {
      hpfPointers[c][s] *= masterAmp;
    }
  }

  // restore previous floating point state
  std::feupdateenv(&fe_state);

  // Let's get outta here
  // This is where we exit mono for whatever the output requires.
  _ProcessOutput(hpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  // _ProcessOutput(lpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  // * Output of input leveling (inputs -> mInputPointers),
  // * Output of output leveling (mOutputPointers -> outputs)
  _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = SafeMaxBlockSize(GetBlockSize());

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, maxBlockSize);
  mToneStack->Reset(sampleRate, maxBlockSize);
  _UpdateLatency();
}

void NeuralAmpModeler::OnIdle()
{
  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      // FIXME -- need to disable only the "normalized" model
      // pGraphics->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
      if (auto* pSettingsBox = pGraphics->GetControlWithTag(kCtrlTagSettingsBox))
        static_cast<NAMSettingsPageControl*>(pSettingsBox)->ClearModelInfo();
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))
        p->Hide(true);
      pGraphics->SetAllControlsDirty();
      mModelCleared = false;
    }
  }
}

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###NeuralAmpModeler###"); // Don't change this!
  chunk.PutStr(header.Get());
  // Plugin version, so we can load legacy serialized states in the future!
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());
  // Model directory (don't serialize the model itself; we'll just load it again
  // when we unserialize)
  chunk.PutStr(mNAMPath.Get());
  chunk.PutStr(mIRPath.Get());
  // ── Phase 10: chain envelope. Older versions of the plug-in stop
  // reading here and go straight into SerializeParams; the reader
  // treats this string as optional so legacy chunks still load.
  // Slot 0's EQ/gain is stored redundantly with the iPlug params so a
  // session saved while mActiveSlot != 0 still restores correctly.
  // Envelope schema:
  //   v=1 — initial Phase 10 envelope (b/m/t/in/out/eq per slot).
  //   v=2 — 2026-05-26: add per-slot "bypassed" (bool, default false)
  //         and "dryWet" (double 0..1, default 1.0). Old (v=1) chunks
  //         load fine — the reader uses .value(key, default) so the
  //         new fields fall through to their defaults when absent.
  nlohmann::json chain = {
    {"v", 2},
    {"active", mActiveSlot},
    {"slot0", {
      {"b",  mSlot0Bass},
      {"m",  mSlot0Mid},
      {"t",  mSlot0Treble},
      {"in", mSlot0InGainDb},
      {"out", mSlot0OutGainDb}
    }},
    {"slots", nlohmann::json::array()},
  };
  for (int i = 0; i < kNumChainSlots - 1; ++i)
  {
    const auto& es = mExtraSlots[i];
    chain["slots"].push_back({
      {"i",   i + 1},
      {"nam", std::string(es.namPath.Get())},
      {"b",   es.bass},
      {"m",   es.mid},
      {"t",   es.treble},
      {"in",  es.inGainDb},
      {"out", es.outGainDb},
      {"eq",  es.eqActive},
      // v=2 additions — appended at the end so older readers that
      // ignore unknown keys still parse the per-slot record cleanly.
      {"bypassed", es.bypassed},
      {"dryWet",   es.dryWet}
    });
  }
  WDL_String chainStr(chain.dump().c_str());
  chunk.PutStr(chainStr.Get());
  return SerializeParams(chunk);
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    return _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    return _UnserializeStateWithUnknownVersion(chunk, startPos);
  }
}

void NeuralAmpModeler::OnUIOpen()
{
  Plugin::OnUIOpen();

  // Restore the persisted window scale from settings.json. The user's
  // last corner-drag size is replayed here so the editor reopens at
  // their preferred zoom.
  //
  // iPlug2 semantics: Resize(w, h, scale) sets the LOGICAL canvas
  // (mWidth/mHeight = w/h) and the draw scale; the host window then
  // becomes w * scale physical pixels. ToneRoot's mRECT is fixed at
  // the design canvas (1664x1040) at construction and iPlug2 doesn't
  // auto-rewrite child mRECTs on Resize, so the logical canvas MUST
  // stay at the design size — otherwise ToneRoot lays out for
  // 1664x1040 but the canvas is smaller and the bottom-right (knob
  // row + part of info pane) clips off the edge. We vary ONLY the
  // scale, never w/h. The corner resizer (EUIResizerMode::Scale)
  // does the same.
  //
  // Default scale = 0.9 (the previous "small" preset, ~1498x936
  // physical pixels). Both this default and the Ctrl+Shift+0 reset
  // path snap back to it.
  if (auto* g = GetUI()) {
    const float scale = ::t3k::settings::instance().window_scale;
    g->Resize(PLUG_WIDTH, PLUG_HEIGHT, scale, /*needsPlatformResize*/ true);
    // Lock down the host wrapper's resize chrome (Win32 only — no-op
    // elsewhere). Done AFTER the initial Resize so the wrapper exists
    // and any size-driven style refresh the host might do has already
    // settled. See StripResizeChrome's comment for the rationale.
    StripResizeChrome(g);
  }

  if (mNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (mModel == nullptr && mStagedModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
  }

  if (mIRPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
    if (mIR == nullptr && mStagedIR == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }
}

void NeuralAmpModeler::OnParentWindowResize(int width, int height)
{
  // The VST3 host (via IPlugVST3_View::onSize) routes here whenever
  // the host's wrapper window changes size — either because the user
  // dragged its edges OR because we asked it to via the corner-
  // resizer drag (IGraphics::Resize -> mView->Resize ->
  // plugFrame->resizeView -> host accepts -> host calls onSize back).
  //
  // We mirror that new pixel size into IGraphics by recomputing the
  // draw scale against the fixed design canvas, picking the SMALLER
  // axis ratio so the design's aspect ratio is preserved and nothing
  // clips. Calling Resize with needsPlatformResize=false closes the
  // loop locally — we do NOT round-trip back to the host (that's the
  // call that produced this notification in the first place).
  //
  // Net effect:
  //   - Corner resizer drag: plugin -> host -> back here -> in-sync.
  //   - Host-driven edge drag: host -> back here -> proportional fit
  //     (effectively only diagonal resize works; one-axis drags get
  //     fitted to the smaller dimension).
  //   - Either path persists the new scale so the next OnUIOpen
  //     restores it.
  auto* g = GetUI();
  if (!g) return;
  if (width <= 0 || height <= 0) return;

  const float sx = static_cast<float>(width)  / static_cast<float>(PLUG_WIDTH);
  const float sy = static_cast<float>(height) / static_cast<float>(PLUG_HEIGHT);
  const float scale = std::min(sx, sy);

  // Skip no-ops to avoid spurious redraws + persistent settings churn
  // when the host echoes our own size back unchanged.
  if (std::abs(scale - g->GetDrawScale()) < 0.001f) return;

  g->Resize(PLUG_WIDTH, PLUG_HEIGHT, scale, /*needsPlatformResize*/ false);

  // Defensive — some hosts may re-apply default styles on certain
  // resize transitions (e.g. when entering/exiting Aero Snap). Re-
  // strip here so the user can't accidentally end up with maximize-
  // chrome restored mid-session. Idempotent: the helper early-outs
  // when the styles are already absent.
  StripResizeChrome(g);

  // Persist the live scale so the user's resize survives a reload.
  ::t3k::settings::instance().window_scale = scale;
  ::t3k::settings::save();
}

void NeuralAmpModeler::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
    // Calibration toggles affect only slot 0's gain stage.
    case kCalibrateInput:
    case kInputCalibrationLevel: _SetInputGain(); break;
    // Input level always drives the global pre-NAM gain stage, but the
    // value also needs to land in whichever slot is currently active
    // so the per-slot shadow is up to date. Guard against re-entrance
    // from _PushActiveSlotIntoParams.
    case kInputLevel:
    {
      if (!mInActiveSlotPush)
      {
        const double v = GetParam(kInputLevel)->Value();
        if (mActiveSlot == 0) {
          mSlot0InGainDb = v;
          _SetInputGain();
        } else if (ExtraSlot* es = _Extra(mActiveSlot)) {
          es->inGainDb = v;
          mInputGain = 1.0;
        }
      }
      break;
    }
    case kOutputLevel:
    {
      if (!mInActiveSlotPush)
      {
        const double v = GetParam(kOutputLevel)->Value();
        if (mActiveSlot == 0) {
          mSlot0OutGainDb = v;
          _SetOutputGain();
        } else if (ExtraSlot* es = _Extra(mActiveSlot)) {
          es->outGainDb = v;
          mOutputGain = 1.0;
        }
      }
      break;
    }
    case kDryWet:
    {
      if (!mInActiveSlotPush)
      {
        const double v = GetParam(kDryWet)->Value();
        if (mActiveSlot == 0)
          mSlot0DryWet = v;
        else if (ExtraSlot* es = _Extra(mActiveSlot))
          es->dryWet = v / 100.0;
      }
      break;
    }
    case kOutputMode:
      if (mActiveSlot == 0) _SetOutputGain();
      break;
    // Tone stack — route to whichever slot is currently active. Empty
    // slots still let the knob move and store the value for later.
    case kToneBass:
    case kToneMid:
    case kToneTreble:
    {
      if (!mInActiveSlotPush)
      {
        const double v = GetParam(paramIdx)->Value();
        const char* tsParam = (paramIdx == kToneBass)   ? "bass"
                            : (paramIdx == kToneMid)    ? "middle"
                                                        : "treble";
        if (mActiveSlot == 0)
        {
          if (paramIdx == kToneBass)        mSlot0Bass   = v;
          else if (paramIdx == kToneMid)    mSlot0Mid    = v;
          else                              mSlot0Treble = v;
          if (mToneStack)
            mToneStack->SetParam(tsParam, v);
        }
        else if (ExtraSlot* es = _Extra(mActiveSlot))
        {
          if (paramIdx == kToneBass)        es->bass   = v;
          else if (paramIdx == kToneMid)    es->mid    = v;
          else                              es->treble = v;
          if (es->toneStack)
            es->toneStack->SetParam(tsParam, v);
        }
      }
      break;
    }
    case kSlim: _ApplySlimParamToLoadedNAMs(); break;
    default: break;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    switch (paramIdx)
    {
      case kNoiseGateActive:
        if (auto* c = pGraphics->GetControlWithParamIdx(kNoiseGateThreshold))
          c->SetDisabled(!active);
        break;
      case kEQActive:
        pGraphics->ForControlInGroup("EQ_KNOBS", [active](IControl* pControl) { pControl->SetDisabled(!active); });
        break;
      case kIRToggle:
        if (auto* c = pGraphics->GetControlWithTag(kCtrlTagIRFileBrowser))
          c->SetDisabled(!active);
        break;
      default: break;
    }
  }
}

bool NeuralAmpModeler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel: mShouldRemoveModel = true; return true;
    case kMsgTagClearIR: mShouldRemoveIR = true; return true;
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

// Private methods ============================================================

void NeuralAmpModeler::_AllocateIOPointers(const size_t nChans)
{
  if (mInputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mInputPointers without freeing");
  mInputPointers = new sample*[nChans];
  if (mInputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mOutputPointers without freeing");
  mOutputPointers = new sample*[nChans];
  if (mOutputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_ApplyDSPStaging()
{
  // Remove marked modules
  if (mShouldRemoveModel)
  {
    mModel = nullptr;
    mNAMPath.Set("");
    mShouldRemoveModel = false;
    mModelCleared = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mShouldRemoveIR)
  {
    mIR = nullptr;
    mIRPath.Set("");
    mShouldRemoveIR = false;
  }
  // Move things from staged to live
  if (mStagedModel != nullptr)
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mNewModelLoadedInDSP = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mStagedIR != nullptr)
  {
    mIR = std::move(mStagedIR);
    mStagedIR = nullptr;
  }
  // ── Phase 10: swap staged/live for every extra slot. A separate slot
  // appearing or disappearing also changes the chain latency, so call
  // _UpdateLatency() once at the end if anything moved.
  bool extrasChanged = false;
  for (auto& es : mExtraSlots)
  {
    if (es.shouldRemove)
    {
      es.model = nullptr;
      es.shouldRemove = false;
      extrasChanged = true;
    }
    if (es.stagedModel != nullptr)
    {
      es.model = std::move(es.stagedModel);
      es.stagedModel = nullptr;
      extrasChanged = true;
    }
    // 2026-05-26 — per-slot IR staging mirror.
    if (es.shouldRemoveIR)
    {
      es.ir = nullptr;
      es.shouldRemoveIR = false;
      extrasChanged = true;
    }
    if (es.stagedIR != nullptr)
    {
      es.ir = std::move(es.stagedIR);
      es.stagedIR = nullptr;
      extrasChanged = true;
    }
  }
  if (extrasChanged)
    _UpdateLatency();
}

void NeuralAmpModeler::_DeallocateIOPointers()
{
  if (mInputPointers != nullptr)
  {
    delete[] mInputPointers;
    mInputPointers = nullptr;
  }
  if (mInputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
  {
    delete[] mOutputPointers;
    mOutputPointers = nullptr;
  }
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      mOutputArray[c][s] = mInputArray[c][s];
}

void NeuralAmpModeler::_ResetModelAndIR(const double sampleRate, const int maxBlockSize)
{
  // Model
  if (mStagedModel != nullptr)
  {
    mStagedModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mModel != nullptr)
  {
    mModel->Reset(sampleRate, maxBlockSize);
  }

  // IR
  if (mStagedIR != nullptr)
  {
    const double irSampleRate = mStagedIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIR != nullptr)
  {
    const double irSampleRate = mIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }

  // ── Phase 10: extras follow the same staged-or-live + tone-stack
  // reset pattern as slot 0.
  for (auto& es : mExtraSlots)
  {
    if (es.stagedModel != nullptr)
    {
      es.stagedModel->Reset(sampleRate, maxBlockSize);
    }
    else if (es.model != nullptr)
    {
      es.model->Reset(sampleRate, maxBlockSize);
    }
    if (es.toneStack)
      es.toneStack->Reset(sampleRate, maxBlockSize);

    // 2026-05-26 — per-slot IR sample-rate handling. Mirrors the
    // single-IR logic above: if the live (or staged) IR was loaded at
    // a different sample rate, rebuild it at the host's current SR by
    // re-feeding its raw data into a fresh ImpulseResponse — same
    // pattern dsp::ImpulseResponse uses for resampling.
    if (es.stagedIR != nullptr)
    {
      if (es.stagedIR->GetSampleRate() != sampleRate)
      {
        const auto irData = es.stagedIR->GetData();
        es.stagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      }
    }
    else if (es.ir != nullptr)
    {
      if (es.ir->GetSampleRate() != sampleRate)
      {
        const auto irData = es.ir->GetData();
        es.stagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      }
    }
  }
}

void NeuralAmpModeler::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void NeuralAmpModeler::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
      default: break;
    }
  }
  mOutputGain = DBToAmp(gainDB);
}

void NeuralAmpModeler::_ApplySlimParamToLoadedNAMs()
{
  const double v = GetParam(kSlim)->Value();
  auto apply = [v](ResamplingNAM* p) {
    if (p == nullptr)
      return;
    if (nam::SlimmableModel* s = p->GetSlimmableModel())
      s->SetSlimmableSize(v);
  };
  apply(mModel.get());
  apply(mStagedModel.get());
}

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mNAMPath;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);

    // Check that the model has 1 input and 1 output channel
    if (model->NumInputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 input channel, but has " + std::to_string(model->NumInputChannels()));
    }
    if (model->NumOutputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 output channel, but has "
                               + std::to_string(model->NumOutputChannels()));
    }

    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), SafeMaxBlockSize(GetBlockSize()));
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
    {
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    }
    mStagedModel = std::move(temp);
    mNAMPath = modelPath;
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
  }
  // 2026-05-26 — widened from std::runtime_error to std::exception (and
  // a defensive catch(...)) because the NAM loader throws other types
  // too — most importantly nlohmann::json::parse_error when a non-NAM
  // file (e.g. a .wav IR mistakenly routed here from the Library) is
  // fed to nam::get_dsp. Letting any throw escape kills the host.
  catch (const std::exception& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  catch (...)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
    if (mStagedModel != nullptr) mStagedModel = nullptr;
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module (unknown exception)" << std::endl;
    return "unknown error loading NAM model";
  }
  return "";
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIR(const WDL_String& irPath)
{
  // FIXME it'd be better for the path to be "staged" as well. Just in case the
  // path and the model got caught on opposite sides of the fence...
  WDL_String previousIRPath = mIRPath;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    mStagedIR = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = mStagedIR->GetWavState();
  }
  // 2026-05-26 — widened, same reasoning as _StageModel above. The IR
  // loader (dsp::ImpulseResponse) can throw std::ios_base::failure for
  // missing files and other std::exception subclasses for corrupt WAV
  // headers; the previous narrower catch let those escape the plugin.
  catch (const std::exception& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }
  catch (...)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unknown exception while attempting to load IR" << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPath = irPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
  }
  else
  {
    if (mStagedIR != nullptr)
    {
      mStagedIR = nullptr;
    }
    mIRPath = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  return wavState;
}

size_t NeuralAmpModeler::_GetBufferNumChannels() const
{
  // Assumes input=output (no mono->stereo effects)
  return mInputArray.size();
}

size_t NeuralAmpModeler::_GetBufferNumFrames() const
{
  if (_GetBufferNumChannels() == 0)
    return 0;
  return mInputArray[0].size();
}

void NeuralAmpModeler::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
  // ── Phase 10: give every chain slot its own tone stack instance.
  // Slot 0 stays on the legacy mToneStack above; mExtraSlots[i] is
  // chain slot (i + 1).
  for (auto& es : mExtraSlots)
  {
    es.toneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
  }
}

// ── Phase 10 chain API ────────────────────────────────────────────

std::string NeuralAmpModeler::StageModelInSlot(int slot, const WDL_String& namPath)
{
  if (slot < 0 || slot >= kNumChainSlots)
    return "invalid slot";
  // Slot 0 routes through the legacy single-model path so all the
  // upstream NAM behavior (calibration broadcast, slimmable knob,
  // settings-page model info, etc.) keeps working unchanged.
  if (slot == 0)
    return _StageModel(namPath);

  ExtraSlot* es = _Extra(slot);
  if (!es)
    return "invalid slot";

  // 2026-05-26 — early-reject non-NAM file extensions. Without this,
  // routing a .wav IR file through here (which the Library does for any
  // tone with kind=="ir" until a slot-aware IR path is built) hits
  // nam::get_dsp(<wav>) which throws nlohmann::json::parse_error and —
  // before this change — escaped the catch below and killed the host.
  // Until the chain learns IRs natively this is a clean no-op: the UI
  // slot stays visible, audio stays untouched.
  {
    const char* p = namPath.Get();
    const std::size_t n = std::strlen(p);
    auto endsWith = [&](const char* sfx) {
      const std::size_t m = std::strlen(sfx);
      if (n < m) return false;
      for (std::size_t i = 0; i < m; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(p[n - m + i])));
        if (a != sfx[i]) return false;
      }
      return true;
    };
    if (!endsWith(".nam"))
    {
      es->stagedModel = nullptr;
      return "non-NAM file (IR or unknown extension); chain stage skipped";
    }
  }

  try
  {
    auto dspPath = std::filesystem::u8path(namPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("Model must have 1 input + 1 output channel");
    auto temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), SafeMaxBlockSize(GetBlockSize()));
    es->stagedModel = std::move(temp);
    es->namPath = namPath;
  }
  // 2026-05-26 — widened from std::runtime_error to std::exception (+
  // catch-all) so json::parse_error / ifstream::failure / etc. can't
  // escape and crash the host. See _StageModel above for the full
  // diagnosis.
  catch (const std::exception& e)
  {
    es->stagedModel = nullptr;
    return e.what();
  }
  catch (...)
  {
    es->stagedModel = nullptr;
    return "unknown error loading NAM model";
  }
  return "";
}

void NeuralAmpModeler::SetChainOrder(const int* order, int count)
{
  if (count < 0)               count = 0;
  if (count > kNumExtraSlots)  count = kNumExtraSlots;
  for (int i = 0; i < count; ++i)
    mPendingChainOrder[i] = order ? order[i] : i;
  mPendingChainOrderLen = count;
  mChainOrderDirty.store(true, std::memory_order_release);
}

void NeuralAmpModeler::UnloadSlot(int slot)
{
  if (slot == 0)
  {
    mShouldRemoveModel = true;
    return;
  }
  if (ExtraSlot* es = _Extra(slot))
  {
    // 2026-05-26 — also arm IR removal so a slot transitioning from
    // an IR back to empty (or to a NAM after the user re-loads) ends
    // up genuinely empty on the audio side.
    es->shouldRemove   = true;
    es->shouldRemoveIR = true;
    es->namPath.Set("");
    es->irPath.Set("");
  }
}

void NeuralAmpModeler::SetActiveSlot(int slot)
{
  if (slot < 0)
    slot = 0;
  if (slot >= kNumChainSlots)
    slot = kNumChainSlots - 1;
  if (slot == mActiveSlot)
    return;
  // Snapshot the currently-active slot's iPlug param values into its
  // shadow store so we don't lose them when we switch.
  if (mActiveSlot == 0)
  {
    mSlot0Bass      = GetParam(kToneBass)->Value();
    mSlot0Mid       = GetParam(kToneMid)->Value();
    mSlot0Treble    = GetParam(kToneTreble)->Value();
    mSlot0InGainDb  = GetParam(kInputLevel)->Value();
    mSlot0OutGainDb = GetParam(kOutputLevel)->Value();
    mSlot0DryWet    = GetParam(kDryWet)->Value();
  }
  else if (ExtraSlot* prev = _Extra(mActiveSlot))
  {
    prev->bass      = GetParam(kToneBass)->Value();
    prev->mid       = GetParam(kToneMid)->Value();
    prev->treble    = GetParam(kToneTreble)->Value();
    prev->inGainDb  = GetParam(kInputLevel)->Value();
    prev->outGainDb = GetParam(kOutputLevel)->Value();
    prev->dryWet    = GetParam(kDryWet)->Value() / 100.0;
  }
  mActiveSlot = slot;
  _PushActiveSlotIntoParams();
}

const char* NeuralAmpModeler::GetSlotNamPath(int slot) const
{
  if (slot == 0)
    return mNAMPath.Get();
  if (const ExtraSlot* es = _Extra(slot))
    return es->namPath.Get();
  return "";
}

bool NeuralAmpModeler::SlotHasModel(int slot) const
{
  if (slot == 0)
    return mModel != nullptr;
  if (const ExtraSlot* es = _Extra(slot))
    return es->model != nullptr;
  return false;
}

// 2026-05-26 — per-slot IR API. Stages a .wav into mExtraSlots[slot-1].
// Slot 0 IRs continue to use the legacy _StageIR path. ToneView's
// syncDspChain calls this when the library row's kind == "ir".
std::string NeuralAmpModeler::StageIRInSlot(int slot, const WDL_String& irPath)
{
  if (slot < 0 || slot >= kNumChainSlots)
    return "invalid slot";
  if (slot == 0)
  {
    // Slot 0 uses the legacy single-IR path. Translate the return so
    // callers see the same string convention as the extra slots.
    const dsp::wav::LoadReturnCode rc = _StageIR(irPath);
    return rc == dsp::wav::LoadReturnCode::SUCCESS ? "" : "IR load failed";
  }
  ExtraSlot* es = _Extra(slot);
  if (!es)
    return "invalid slot";
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    const double sr = GetSampleRate();
    auto temp = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sr);
    if (temp->GetWavState() != dsp::wav::LoadReturnCode::SUCCESS)
    {
      return "IR file rejected by loader";
    }
    es->stagedIR = std::move(temp);
    // 2026-05-26 — clear any NAM on this slot. A slot can hold ONE
    // file; staging an IR over a NAM evicts the model on the next
    // audio tick.
    es->shouldRemove = true;
    es->irPath       = irPath;
    es->namPath.Set("");
  }
  catch (const std::exception& e)
  {
    es->stagedIR = nullptr;
    return e.what();
  }
  catch (...)
  {
    es->stagedIR = nullptr;
    return "unknown error loading IR";
  }
  return "";
}

const char* NeuralAmpModeler::GetSlotIRPath(int slot) const
{
  if (slot == 0)
    return mIRPath.Get();
  if (const ExtraSlot* es = _Extra(slot))
    return es->irPath.Get();
  return "";
}

bool NeuralAmpModeler::SlotHasIR(int slot) const
{
  if (slot == 0)
    return mIR != nullptr;
  if (const ExtraSlot* es = _Extra(slot))
    return es->ir != nullptr;
  return false;
}

void NeuralAmpModeler::_PushActiveSlotIntoParams()
{
  mInActiveSlotPush = true;
  double bass = 5.0, mid = 5.0, treble = 5.0, inDb = 0.0, outDb = 0.0, dryWetPct = 100.0;
  if (mActiveSlot == 0)
  {
    bass = mSlot0Bass;
    mid = mSlot0Mid;
    treble = mSlot0Treble;
    inDb = mSlot0InGainDb;
    outDb = mSlot0OutGainDb;
    dryWetPct = mSlot0DryWet;
  }
  else if (const ExtraSlot* es = _Extra(mActiveSlot))
  {
    bass = es->bass;
    mid = es->mid;
    treble = es->treble;
    inDb = es->inGainDb;
    outDb = es->outGainDb;
    dryWetPct = es->dryWet * 100.0;
  }
  else
  {
    mInActiveSlotPush = false;
    return;
  }
  // SendParameterValueFromDelegate informs the host + redraws the
  // bound knob without firing OnParamChange recursion (we still guard
  // with mInActiveSlotPush as belt-and-suspenders). The trailing
  // `true` means the value we pass is already normalized 0..1.
  SendParameterValueFromDelegate(kToneBass,    GetParam(kToneBass)   ->ToNormalized(bass),   true);
  SendParameterValueFromDelegate(kToneMid,     GetParam(kToneMid)    ->ToNormalized(mid),    true);
  SendParameterValueFromDelegate(kToneTreble,  GetParam(kToneTreble) ->ToNormalized(treble), true);
  SendParameterValueFromDelegate(kInputLevel,  GetParam(kInputLevel) ->ToNormalized(inDb),   true);
  SendParameterValueFromDelegate(kOutputLevel, GetParam(kOutputLevel)->ToNormalized(outDb),  true);
  SendParameterValueFromDelegate(kDryWet,      GetParam(kDryWet)     ->ToNormalized(dryWetPct), true);
  // Also push directly into GetParam so subsequent reads work.
  GetParam(kToneBass)   ->Set(bass);
  GetParam(kToneMid)    ->Set(mid);
  GetParam(kToneTreble) ->Set(treble);
  GetParam(kInputLevel) ->Set(inDb);
  GetParam(kOutputLevel)->Set(outDb);
  GetParam(kDryWet)     ->Set(dryWetPct);
  // Reapply EQ to the now-active slot's tone stack with these values.
  _ApplyEqToSlot(mActiveSlot);
  if (mActiveSlot == 0) {
    _SetInputGain();
    _SetOutputGain();
  } else {
    mInputGain = 1.0;
    mOutputGain = 1.0;
  }
  mInActiveSlotPush = false;
}

void NeuralAmpModeler::_ApplyEqToSlot(int slot)
{
  if (slot == 0)
  {
    if (!mToneStack)
      return;
    mToneStack->SetParam("bass",   mSlot0Bass);
    mToneStack->SetParam("middle", mSlot0Mid);
    mToneStack->SetParam("treble", mSlot0Treble);
  }
  else if (ExtraSlot* es = _Extra(slot))
  {
    if (!es->toneStack)
      return;
    es->toneStack->SetParam("bass",   es->bass);
    es->toneStack->SetParam("middle", es->mid);
    es->toneStack->SetParam("treble", es->treble);
  }
}

void NeuralAmpModeler::_PrepareBuffers(const size_t numChannels, const size_t numFrames)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool updateFrames = updateChannels || (_GetBufferNumFrames() != numFrames);
  //  if (!updateChannels && !updateFrames)  // Could we do this?
  //    return;

  if (updateChannels)
  {
    _PrepareIOPointers(numChannels);
    mInputArray.resize(numChannels);
    mOutputArray.resize(numChannels);
  }
  if (updateFrames)
  {
    for (auto c = 0; c < mInputArray.size(); c++)
    {
      mInputArray[c].resize(numFrames);
      std::fill(mInputArray[c].begin(), mInputArray[c].end(), 0.0);
    }
    for (auto c = 0; c < mOutputArray.size(); c++)
    {
      mOutputArray[c].resize(numFrames);
      std::fill(mOutputArray[c].begin(), mOutputArray[c].end(), 0.0);
    }
  }
  // Would these ever get changed by something?
  for (auto c = 0; c < mInputArray.size(); c++)
    mInputPointers[c] = mInputArray[c].data();
  for (auto c = 0; c < mOutputArray.size(); c++)
    mOutputPointers[c] = mOutputArray[c].data();
}

void NeuralAmpModeler::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // We'll assume that the main processing is mono for now. We'll handle dual amps later.
  if (nChansOut != 1)
  {
    std::stringstream ss;
    ss << "Expected mono output, but " << nChansOut << " output channels are requested!";
    throw std::runtime_error(ss.str());
  }

  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  gain /= (float)nChansIn;
#endif
  // Assume _PrepareBuffers() was already called
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
      if (c == 0)
        mInputArray[0][s] = gain * inputs[c][s];
      else
        mInputArray[0][s] += gain * inputs[c][s];
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  const double gain = mOutputGain;
  // Assume _PrepareBuffers() was already called
  if (nChansIn != 1)
    throw std::runtime_error("Plugin is supposed to process in mono.");
  // Broadcast the internal mono stream to all output channels.
  const size_t cin = 0;
  for (auto cout = 0; cout < nChansOut; cout++)
    for (auto s = 0; s < nFrames; s++)
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = std::clamp(gain * inputs[cin][s], -1.0, 1.0);
#else // In a DAW, other things may come next and should be able to handle large
      // values.
      outputs[cout][s] = gain * inputs[cin][s];
#endif
}

void NeuralAmpModeler::_UpdateControlsFromModel()
{
  if (mModel == nullptr)
  {
    return;
  }
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    modelInfo.sampleRate.known = true;
    modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
    modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
    modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
    modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
    modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;

    if (auto* pSettingsBox = pGraphics->GetControlWithTag(kCtrlTagSettingsBox))
      static_cast<NAMSettingsPageControl*>(pSettingsBox)->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !mModel->HasInputLevel();
    if (auto* c = pGraphics->GetControlWithTag(kCtrlTagCalibrateInput))
      c->SetDisabled(disableInputCalibrationControls);
    if (auto* c = pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel))
      c->SetDisabled(disableInputCalibrationControls);
    if (auto* pOutputMode = pGraphics->GetControlWithTag(kCtrlTagOutputMode)) {
      auto* c = static_cast<OutputModeControl*>(pOutputMode);
      c->SetNormalizedDisable(!mModel->HasLoudness());
      c->SetCalibratedDisable(!mModel->HasOutputLevel());
    }

    if (auto* pSlimIcon = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
    {
      const bool show = mModel->GetSlimmableModel() != nullptr;
      pSlimIcon->Hide(!show);
    }
  }
}

void NeuralAmpModeler::_UpdateLatency()
{
  int latency = 0;
  if (mModel)
  {
    latency += mModel->GetLatency();
  }
  // ── Phase 10: chain latency is the sum of all live slot latencies.
  for (const auto& es : mExtraSlots)
  {
    if (es.model)
      latency += es.model->GetLatency();
  }
  // Other things that add latency here...

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void NeuralAmpModeler::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansIn, const size_t nChansOut)
{
  // Right now, we didn't specify MAXNC when we initialized these, so it's 1.
  const int nChansHack = 1;
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, nChansHack);
  mOutputSender.ProcessBlock(outputPointer, (int)nFrames, kCtrlTagOutputMeter, nChansHack);
}

// HACK
#include "Unserialization.cpp"
