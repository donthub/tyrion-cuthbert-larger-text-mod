// Abstract view of the game's UI objects.
//
// Features are written against this interface only. The game implementation
// (engine/unity_ui.cpp) talks to Unity through IL2CPP; the tests use an
// in-memory stand-in (tests/fake_ui.h). Keeping features engine-agnostic is what
// makes them testable without game files.
#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace ltm {

struct Vec2 {
    float x = 0, y = 0;
};

struct RectData {
    Vec2 anchorMin, anchorMax, anchoredPosition, sizeDelta, pivot;
};

// An axis-aligned box in some object's local space.
struct Bounds {
    float x_min = 0, y_min = 0, x_max = 0, y_max = 0;
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
    // The game's Settings menu (namespace UIBase) and TMP dropdowns.
    SettingsMenu,
    SettingsTab,
    LoadTabOnSelect,
    LocalizedText,
    SetActiveOnPlatforms,
    CustomSettingDropdown,
    Dropdown,        // TMPro.TMP_Dropdown
    // The game's Save/Load menu (namespace VisualNovel) and its slot buttons.
    SaveMenu,
    SaveMenuButton,
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
    TmpLineHeight,       // read-only: height of one line, incl. margins
    TmpLinePitch,        // read-only: baseline distance of wrapped lines
    // SuperTextMesh
    StmSize,
    StmCharacterSpacing,
    StmRememberReadPosition,
    StmReading,          // read-only
    StmReadTime,         // read-only: current reveal position
    StmTotalReadTime,    // read-only: time when fully revealed
    StmBestFit,          // enum SuperTextMesh.BestFitMode
    StmUiLimit,          // use the rect height as the vertical limit
    StmVerticalLimitMode,  // read-only, enum SuperTextMesh.VerticalLimitMode
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
    // TMP_Dropdown
    DropdownValue,     // selected index; writing it doesn't fire the change event
    DropdownExpanded,  // read-only: the option list is open
    // SaveMenuButton (read-only fields)
    SaveEmpty,         // is_empty: the slot holds no save
    SaveIndex,         // saveIndex: the slot's index
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
    virtual void set_name(Handle go, const std::string &name) = 0;
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
    // The corners of `go`'s rect as laid out, in `ref`'s local space.
    virtual Bounds bounds_in(Handle go, Handle ref) = 0;

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
    // The text as the game assigned it (TMP), UTF-8, including rich-text tags.
    virtual std::string text(Handle go, Comp c) = 0;
    // SuperTextMesh: redraws the text as revealed up to `read_time`, without
    // starting, stopping or firing any reading events.
    virtual void show_text_until(Handle go, double read_time) = 0;
    // The value of a member of an enum-typed property's enum, e.g. a
    // SuperTextMesh.BestFitMode value by name. Throws Mismatch when absent.
    virtual int enum_value(Handle go, Comp c, Prop p, const std::string &member) = 0;
    // Sets a TMP text (UTF-8).
    virtual void set_text(Handle go, const std::string &text) = 0;
    // Replaces the options of the TMP_Dropdown on `go`.
    virtual void set_dropdown_options(Handle go, const std::vector<std::string> &labels) = 0;
    // String and string[] fields of a component, by field name.
    virtual std::string get_string(Handle go, Comp c, const std::string &field) = 0;
    virtual void set_string(Handle go, Comp c, const std::string &field, const std::string &value) = 0;
    virtual void set_strings(Handle go, Comp c, const std::string &field, const std::vector<std::string> &values) = 0;
    // An inactive object that survives scene loads. Objects below it don't run
    // their scripts (no Awake), so it can hold templates. Always the same one.
    virtual Handle holder() = 0;

    // The game's Settings menu (UIBase.SettingsMenu on `menu`). Its tabs are a
    // list of {name, tab prefab, button}; the game instantiates a tab's prefab
    // when its button is selected.
    virtual std::vector<std::string> settings_tabs(Handle menu) = 0;
    virtual Handle settings_tab_prefab(Handle menu, const std::string &tab) = 0;  // nullptr when missing
    virtual void add_settings_tab(Handle menu, const std::string &tab, Handle prefab, Handle button) = 0;
    virtual void remove_settings_tab(Handle menu, const std::string &tab) = 0;
    virtual std::string current_settings_tab(Handle menu) = 0;
    virtual Handle loaded_settings_tab(Handle menu) = 0;  // the shown tab instance, nullptr when none
    // The options a tab navigates (SettingsTab.allSettings, firstSelect = the first).
    // Each object carries a settings option (CustomSettingDropdown).
    virtual void set_settings_options(Handle tab, const std::vector<Handle> &options) = 0;
};

}  // namespace ltm
