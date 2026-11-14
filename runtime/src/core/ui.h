// Abstract view of the game's UI objects.
//
// Features are written against this interface only. The game implementation
// (engine/unity_ui.cpp) talks to Unity through IL2CPP; the tests use an
// in-memory stand-in (tests/fake_ui.h). Keeping features engine-agnostic is what
// makes them testable without game files.
#pragma once
#include <stdexcept>
#include <string>

namespace ltm {

struct Vec2 {
    float x = 0, y = 0;
};

struct RectData {
    Vec2 anchorMin, anchorMax, anchoredPosition, sizeDelta, pivot;
};

// An opaque GameObject reference. nullptr means "none".
using Handle = void *;

// Component kinds the features use.
enum class Comp {
    Tmp,             // TMPro.TMP_Text (TextMeshProUGUI)
    Stm,             // SuperTextMesh
    RawImage,        // UnityEngine.UI.RawImage
    Graphic,         // UnityEngine.UI.Graphic (any)
    HLayout,         // UnityEngine.UI.HorizontalLayoutGroup
    VLayout,         // UnityEngine.UI.VerticalLayoutGroup
    LayoutElement,   // UnityEngine.UI.LayoutElement
    SizeFitter,      // UnityEngine.UI.ContentSizeFitter
    AspectFitter,    // UnityEngine.UI.AspectRatioFitter
    Button,          // UnityEngine.UI.Button
    // Game controllers, used to identify instances.
    DialogueBox,
    TestimonyPrompt,
    TextLogGUI,
    ItemMenu,
    AddItemDisplay,
    ImageDisplay,
    Count
};

const char *comp_name(Comp c);

// Scalar properties. Booleans are 0/1, enums are their integer value.
enum class Prop {
    Enabled,  // Behaviour.enabled
    // TMP_Text
    TmpFontSize,
    TmpAutoSize,
    TmpFontSizeMin,
    TmpFontSizeMax,
    TmpCharacterSpacing,
    TmpOverflowMode,
    TmpPreferredHeight,  // read-only, for the current rect width
    TmpRenderedHeight,   // read-only, after a mesh update
    TmpOverflowing,      // read-only
    // SuperTextMesh
    StmSize,
    StmCharacterSpacing,
    StmRememberReadPosition,
    StmReading,          // read-only
    StmReadTime,         // read-only: current reveal position
    StmTotalReadTime,    // read-only: time when fully revealed
    // Graphic
    GraphicAlpha,
    // HorizontalOrVerticalLayoutGroup
    LgSpacing,
    LgChildAlignment,
    LgControlWidth,
    LgControlHeight,
    LgExpandWidth,
    LgExpandHeight,
    // LayoutElement
    LeIgnoreLayout,
    LeMinWidth,
    LeMinHeight,
    LePreferredWidth,
    LePreferredHeight,
    LeFlexibleWidth,
    LeFlexibleHeight,
    // ContentSizeFitter (0 unconstrained, 1 min, 2 preferred)
    FitHorizontal,
    FitVertical,
    // AspectRatioFitter (2 = height controls width)
    AspectMode,
    Count
};

const char *prop_name(Prop p);

// TMP overflow modes used here.
constexpr int kOverflowOverflow = 0;
constexpr int kOverflowLinked = 6;
constexpr int kFitPreferred = 2;
constexpr int kAspectHeightControlsWidth = 2;

// A structure check failed: the game's UI is not what the feature expects.
struct Mismatch : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The engine reported an error (e.g. a managed exception).
struct EngineError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Ui {
  public:
    virtual ~Ui() = default;

    // Hierarchy. `path` is relative to `root`, with '/' separators; "" is root.
    virtual Handle find(Handle root, const std::string &path) = 0;
    virtual std::string name(Handle go) = 0;
    virtual Handle parent(Handle go) = 0;
    virtual int sibling_index(Handle go) = 0;
    virtual int child_count(Handle go) = 0;
    virtual Handle child(Handle go, int index) = 0;
    // Reparents without changing the local layout values.
    virtual void set_parent(Handle go, Handle new_parent, int sibling_index) = 0;
    virtual bool active_self(Handle go) = 0;
    virtual bool active_in_hierarchy(Handle go) = 0;
    virtual int instance_id(Handle go) = 0;
    virtual bool alive(Handle go) = 0;
    // Keeps a handle valid across frames (until release). Handles returned by
    // other calls are only guaranteed valid during the current call into the core.
    virtual void retain(Handle go) = 0;
    virtual void release(Handle go) = 0;
    virtual void set_active(Handle go, bool active) = 0;

    // Components.
    virtual bool has(Handle go, Comp c) = 0;
    virtual void add(Handle go, Comp c) = 0;
    virtual void remove(Handle go, Comp c) = 0;
    virtual double get(Handle go, Comp c, Prop p) = 0;
    virtual void set(Handle go, Comp c, Prop p, double value) = 0;

    // RectTransform.
    virtual RectData rect(Handle go) = 0;
    virtual void set_rect(Handle go, const RectData &r) = 0;
    virtual Vec2 rect_size(Handle go) = 0;  // the computed rect width/height
    virtual Vec2 local_scale(Handle go) = 0;

    // Objects.
    virtual Handle create(const std::string &name, Handle parent) = 0;  // empty UI object
    virtual Handle clone(Handle original, Handle parent, const std::string &name) = 0;
    virtual void destroy(Handle go) = 0;

    // Text.
    virtual Handle linked_text(Handle go) = 0;  // TMP linked overflow target (its GameObject)
    virtual void set_linked_text(Handle go, Handle target) = 0;
    virtual void update_text(Handle go, Comp c) = 0;    // TMP ForceMeshUpdate / STM Rebuild
    // Changes whenever the game assigns new text (TMP or SuperTextMesh).
    virtual const void *text_identity(Handle go, Comp c) = 0;
    // SuperTextMesh: redraws the text as revealed up to `read_time`, without
    // starting, stopping or firing any reading events.
    virtual void show_text_until(Handle go, double read_time) = 0;
};

}  // namespace ltm
