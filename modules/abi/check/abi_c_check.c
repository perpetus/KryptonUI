/* Compiles abi.h as C so the contract stays usable from plain C hosts. */
#include "krypton_ui/abi.h"
_Static_assert(KR_SUGGESTION_RICH_IMAGE == 1u << 3,
               "rich image flag is append-only");
_Static_assert(offsetof(KrSuggestion, contents_class) >
                   offsetof(KrSuggestion, icon),
               "suggestion classifications are an optional tail");
_Static_assert(offsetof(KrTab, folder) > offsetof(KrTab, favicon),
               "folder membership is an optional tab tail");
_Static_assert(offsetof(KrBrowser, folder_command) >
                   offsetof(KrBrowser, request_glass_backdrop),
               "folder commands append to the browser table");
_Static_assert(offsetof(KrShellUI, upsert_folder) >
                   offsetof(KrShellUI, show_split_options),
               "folder updates append to the UI table");
_Static_assert(KR_DOWNLOAD_STARTED == 1u << 4,
               "download-start remains append-only");
_Static_assert(KR_SUGGESTION_SELECTED == 1u << 2,
               "selection flag remains append-only");
_Static_assert(KR_ACTION_COPY_LINK == 6,
               "copy-link action remains append-only");

_Static_assert(KR_SETTINGS_ABOUT == 7 && KR_SETTINGS_WORKSPACES == 8,
               "settings section values remain append-only");
_Static_assert(offsetof(KrSettingsSnapshot, struct_size) == 0,
               "sized settings snapshot");
_Static_assert(offsetof(KrSettingsEdit, struct_size) == 0,
               "sized settings edit");
_Static_assert(offsetof(KrSettingsOperationResult, struct_size) == 0,
               "sized settings completion");
_Static_assert(offsetof(KrBrowser, settings_visibility_changed) >
                   offsetof(KrBrowser, onboarding_finished),
               "settings callbacks append after onboarding");
_Static_assert(offsetof(KrShellUI, set_settings_snapshot) >
                   offsetof(KrShellUI, remove_search_engine),
               "settings UI callbacks append after browser services");

static const size_t kShellUiSize = sizeof(KrShellUI);
static const size_t kBrowserSize = sizeof(KrBrowser);
_Static_assert(KR_ACTION_SITE_TOOLS == 5,
               "site tools appends the action kinds");
_Static_assert(offsetof(KrShellUI, show_task_manager) >
                   offsetof(KrShellUI, show_menu_panel_with_tiles),
               "browser tool slots append the UI table");
_Static_assert(offsetof(KrBrowser, permission_prompt_response) >
                   offsetof(KrBrowser, edit_workspace),
               "browser tool callbacks append the host table");
_Static_assert(KR_PERMISSION_ALLOW_ONCE == 0 && KR_PERMISSION_DISMISS == 3,
               "permission decisions have stable C values");
_Static_assert(sizeof(KrUiShutdownFn) == sizeof(void (*)(void)),
               "shutdown export uses a plain C function pointer");
_Static_assert(offsetof(KrShellUI, set_load_progress) >
                   offsetof(KrShellUI, show_menu_panel),
               "load progress slot must be appended");
_Static_assert(sizeof(((KrLoadProgress*)0)->progress) == sizeof(float),
               "load progress uses a C float");
_Static_assert(offsetof(KrShellUI, show_menu_panel) >
                   offsetof(KrShellUI, show_theme_editor),
               "panel menu slot must be appended");
_Static_assert(KR_MENU_ITEM_HEADER == 6 && KR_MENU_ITEM_CONTROLS == 7,
               "panel row values remain stable");
_Static_assert(KR_MENU_ITEM_GRID == 8 && KR_MENU_ITEM_SECTION == 9 &&
                   KR_MENU_ITEM_FOOTER == 10,
               "tiled panel rows append enum values");
_Static_assert(offsetof(KrShellUI, show_menu_panel_with_tiles) >
                   offsetof(KrShellUI, show_workspace_editor),
               "tiled panel capability must be appended");
_Static_assert(offsetof(KrShellUI, set_workspace_theme) >
                   offsetof(KrShellUI, set_tab_preview),
               "theme slots must be appended");
_Static_assert(offsetof(KrBrowser, set_workspace_theme) >
                   offsetof(KrBrowser, cancel_tab_preview),
               "theme callbacks must be appended");
_Static_assert(sizeof(((KrWorkspaceTheme*)0)->points) / sizeof(KrThemePoint) ==
                   3,
               "theme point capacity");

_Static_assert(offsetof(KrShellUI, set_tab_preview) >
                   offsetof(KrShellUI, show_menu_with_glyphs),
               "tab preview UI slot must be appended");
_Static_assert(offsetof(KrBrowser, request_tab_preview) >
                   offsetof(KrBrowser, set_window_background),
               "tab preview host slots must be appended");

_Static_assert(KR_UI_ABI_VERSION == 2u, "extensible records require ABI v2");
_Static_assert(offsetof(KrCreateParams, initial_theme) >
                   offsetof(KrCreateParams, browser),
               "optional initial theme appends to the creation prefix");
_Static_assert(offsetof(KrBrowser, unhandled_key) >
                   offsetof(KrBrowser, set_appearance_prefs),
               "shortcut intent appends");
#define KR_CHECK_RECORD(type)                                             \
  _Static_assert(offsetof(type, struct_size) == 0, #type " size header"); \
  _Static_assert(sizeof(((type*)0)->struct_size) == sizeof(size_t),       \
                 #type " size uses size_t")
KR_CHECK_RECORD(KrImage);
KR_CHECK_RECORD(KrTab);
KR_CHECK_RECORD(KrTabPreview);
_Static_assert(offsetof(KrTabPreview, title) > offsetof(KrTabPreview, flags),
               "hover labels append the existing preview prefix");
_Static_assert(KR_TAB_PREVIEW_LABELS_PROVIDED == 1u << 4 &&
                   KR_TAB_PREVIEW_NO_IMAGE == 1u << 5,
               "hover presentation flags have stable C values");
KR_CHECK_RECORD(KrSpace);
KR_CHECK_RECORD(KrNavState);
KR_CHECK_RECORD(KrAction);
KR_CHECK_RECORD(KrTheme);
KR_CHECK_RECORD(KrWindowState);
KR_CHECK_RECORD(KrLayoutPrefs);
KR_CHECK_RECORD(KrSuggestion);
KR_CHECK_RECORD(KrMenu);
KR_CHECK_RECORD(KrMenuItem);
KR_CHECK_RECORD(KrMenuItemMetadata);
KR_CHECK_RECORD(KrKeyboardAccelerator);
KR_CHECK_RECORD(KrMenuFont);
KR_CHECK_RECORD(KrMenuColor);
KR_CHECK_RECORD(KrAddressState);
KR_CHECK_RECORD(KrAddressEdit);
KR_CHECK_RECORD(KrNativeFocus);
_Static_assert(offsetof(KrShellUI, focus_native) >
                   offsetof(KrShellUI, set_address_edit),
               "semantic native focus appends to ABI v2");
_Static_assert(offsetof(KrBrowser, select_suggestion_snapshot) >
                   offsetof(KrBrowser, permission_prompt_response),
               "native suggestion selection appends to the host table");
_Static_assert(offsetof(KrAddressState, selection_start) >
                   offsetof(KrAddressState, text),
               "native address state extends the sized record");
_Static_assert(offsetof(KrShellUI, set_address_edit) >
                   offsetof(KrShellUI, dismiss_permission_prompt),
               "model address editing appends to ABI v2");
KR_CHECK_RECORD(KrGradientLayer);
KR_CHECK_RECORD(KrWindowBackdrop);
KR_CHECK_RECORD(KrShellMetrics);
_Static_assert(offsetof(KrBrowser, address_state_changed) >
                   offsetof(KrBrowser, unhandled_key),
               "integration callbacks append to ABI v2");
_Static_assert(offsetof(KrShellUI, set_address_text) >
                   offsetof(KrShellUI, show_settings),
               "integration exports append to ABI v2");
#undef KR_CHECK_RECORD

size_t kr_abi_c_check(void) {
  _Static_assert(offsetof(KrAppearancePrefs, struct_size) == 0,
                 "appearance starts with its size");
  _Static_assert(offsetof(KrShellUI, set_appearance_prefs) >
                     offsetof(KrShellUI, set_load_progress),
                 "settings UI slots append");
  _Static_assert(offsetof(KrBrowser, set_appearance_prefs) >
                     offsetof(KrBrowser, set_window_theme),
                 "appearance host slot appends");
  _Static_assert(KR_LAYOUT_TABS_CLASSIC == 1u << 9, "classic layout flag");
  _Static_assert(KR_LAYOUT_TABS_COMPACT == 1u << 10, "compact layout flag");
  _Static_assert(KR_LAYOUT_TABS_DYNAMIC == 1u << 11, "dynamic layout flag");
  _Static_assert(KR_LAYOUT_COMPACT_TARGETS_EXPLICIT == 1u << 15,
                 "pinning flag");
  _Static_assert(offsetof(KrLayoutPrefs, struct_size) == 0,
                 "layout starts with its size");
  return kShellUiSize + kBrowserSize;
}

_Static_assert(offsetof(KrPrivacyFeature, struct_size) == 0, "sized feature");
_Static_assert(offsetof(KrBrowser, privacy_features_visibility_changed) >
                   offsetof(KrBrowser, select_suggestion_snapshot),
               "append browser slots");
_Static_assert(offsetof(KrShellUI, upsert_privacy_feature) >
                   offsetof(KrShellUI, focus_native),
               "append UI slots");

_Static_assert(offsetof(KrDownload, struct_size) == 0, "sized download");
_Static_assert(offsetof(KrMediaSession, struct_size) == 0, "sized media");
_Static_assert(offsetof(KrLibrarySectionState, struct_size) == 0,
               "sized library section");
_Static_assert(offsetof(KrLibraryItem, struct_size) == 0, "sized library item");
_Static_assert(KR_MENU_LIBRARY_ITEM == 18, "library menu kinds append");
_Static_assert(offsetof(KrBrowser, download_command) >
                   offsetof(KrBrowser, relaunch_browser),
               "library browser slots append after privacy");
_Static_assert(offsetof(KrShellUI, upsert_download) >
                   offsetof(KrShellUI, remove_privacy_feature),
               "library UI slots append after privacy");
_Static_assert(offsetof(KrShellUI, show_library) >
                   offsetof(KrShellUI, remove_library_item),
               "show_library is the last library slot");
_Static_assert(offsetof(KrAppearancePrefs, icon_pack) >
                   offsetof(KrAppearancePrefs, flags),
               "icon pack appends to appearance preferences");
typedef struct KrLegacyAppearancePrefs {
  size_t struct_size;
  uint32_t color_scheme;
  uint32_t menu_material;
  uint32_t flags;
} KrLegacyAppearancePrefs;
_Static_assert(offsetof(KrAppearancePrefs, icon_pack) >=
                   sizeof(KrLegacyAppearancePrefs),
               "icon pack must follow the complete legacy record and padding");
_Static_assert(sizeof(((KrAppearancePrefs*)0)->icon_pack) == sizeof(uint64_t),
               "icon pack extension uses an eight-byte field");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(KrAppearancePrefs, icon_pack) == 24,
               "x64 icon pack follows the legacy 24-byte record");
_Static_assert(sizeof(KrAppearancePrefs) == 40,
               "x64 appearance record with optional glass mode is 40 bytes");
_Static_assert(offsetof(KrAppearancePrefs, glass_mode) == 32,
               "glass mode follows the complete legacy appearance record");
#endif
_Static_assert(KR_ICON_PACK_FLUENT == 0, "Fluent is the default icon pack");
_Static_assert(KR_MEDIA_CAN_TOGGLE_CAMERA == 1u << 12,
               "capture toggles append to media flags");

_Static_assert(offsetof(KrOnboardingRequest, struct_size) == 0,
               "sized KrOnboardingRequest");
_Static_assert(offsetof(KrImportSource, struct_size) == 0,
               "sized KrImportSource");
_Static_assert(offsetof(KrImportProgress, struct_size) == 0,
               "sized KrImportProgress");
_Static_assert(offsetof(KrDefaultBrowserState, struct_size) == 0,
               "sized KrDefaultBrowserState");
_Static_assert(offsetof(KrSearchEngine, struct_size) == 0,
               "sized KrSearchEngine");
_Static_assert(offsetof(KrOnboardingResult, struct_size) == 0,
               "sized KrOnboardingResult");
_Static_assert(offsetof(KrBrowser, onboarding_visibility_changed) >
                   offsetof(KrBrowser, library_command),
               "onboarding browser slots append after the library");
_Static_assert(offsetof(KrBrowser, onboarding_finished) >
                   offsetof(KrBrowser, set_default_search_engine),
               "onboarding_finished is the last onboarding browser slot");
_Static_assert(offsetof(KrShellUI, show_onboarding) >
                   offsetof(KrShellUI, show_library),
               "onboarding UI slots append after the library");
_Static_assert(offsetof(KrShellUI, remove_search_engine) >
                   offsetof(KrShellUI, upsert_search_engine),
               "remove_search_engine is the last onboarding UI slot");
_Static_assert(KR_ONBOARDING_STEP_IGNITION == 0 &&
                   KR_ONBOARDING_STEP_READY == 7,
               "onboarding steps have stable C values");
_Static_assert(KR_ONBOARDING_COMPLETED == 0 && KR_ONBOARDING_CLOSED == 2,
               "onboarding outcomes have stable C values");
_Static_assert(KR_IMPORT_EXTENSIONS == 1u << 5 &&
                   KR_DEFAULT_BROWSER_PENDING == 1u << 3 &&
                   KR_SEARCH_ENGINE_MANAGED == 1u << 1,
               "onboarding flags have stable C values");
_Static_assert(sizeof(((KrImportProgress*)0)->fraction) == sizeof(float),
               "import progress uses a C float");
_Static_assert(offsetof(KrMultiPageMetrics, glance_button_size) >
                   offsetof(KrMultiPageMetrics, outline_width),
               "Glance metrics append after the legacy split metrics");
_Static_assert(offsetof(KrWindowBackdrop, glance_button) >
                   offsetof(KrWindowBackdrop, pane_outline),
               "Glance colors append after the legacy backdrop");
_Static_assert(
    offsetof(KrShellUI, layout_glance_card) >
        offsetof(KrShellUI, layout_glance),
    "card geometry is an optional append; legacy layout is retained");
_Static_assert(offsetof(KrShellUI, show_split_chooser_for_layout) >
                   offsetof(KrShellUI, layout_glance_card),
               "snap chooser appends without changing existing ABI-v2 slots");
_Static_assert(KR_MULTI_PAGE_COMPLETE == 31 &&
                   KR_MULTI_PAGE_SNAP_LAYOUTS == 32 && KR_SPLIT_GRID == 2 &&
                   KR_SPLIT_MAIN_BOTTOM == 9,
               "snap support keeps legacy capabilities and layouts stable");
