#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/core/result.hpp"
#include "krypton/ui/model/browser_services.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/model/settings.hpp"
#include "krypton/ui/model/workspace_theme.hpp"

// First-run onboarding. The real window is the canvas: every moment asks one
// question and the host confirms each answer through its usual updates.
namespace kr {
using ImportSourceId = Id<struct ImportSourceTag>;

enum class OnboardingStep : std::uint8_t {
  kIgnition,
  kShape,
  kColor,
  kImport,
  kSearch,
  kPrivacy,
  kDefaultBrowser,
  kReady
};
inline constexpr std::size_t kOnboardingStepCount = 8;

enum class OnboardingAction : std::uint8_t {
  kNext,
  kBack,
  kSkip,
  kRecommended,
  kFinish
};
enum class OnboardingOutcome : std::uint32_t { kCompleted, kSkipped, kClosed };

enum class ImportData : std::uint32_t {
  kBookmarks = 1u << 0,
  kHistory = 1u << 1,
  kPasswords = 1u << 2,
  kSearchEngines = 1u << 3,
  kAutofill = 1u << 4,
  kExtensions = 1u << 5
};
template <>
inline constexpr bool kIsFlagEnum<ImportData> = true;
inline constexpr std::size_t kImportDataCount = 6;
inline constexpr auto kAllImportData =
    Flags<ImportData>::FromBits((1u << kImportDataCount) - 1);

struct ImportSource {
  ImportSourceId id;
  Flags<ImportData> items;
  std::uint32_t order = 0;
  std::string name;
  ImageId icon;
};
enum class ImportPhase : std::uint32_t { kIdle, kRunning, kSucceeded, kFailed };
struct ImportProgress {
  ImportSourceId source;
  ImportPhase phase = ImportPhase::kIdle;
  float fraction = -1;  // negative: unknown
  friend bool operator==(const ImportProgress&,
                         const ImportProgress&) = default;
};

struct OnboardingCapabilities {
  bool layout = false, theme = false, import = false, search = false,
       privacy = false, default_browser = false;
  friend bool operator==(const OnboardingCapabilities&,
                         const OnboardingCapabilities&) = default;
};

// Chrome elements an answer can land on, in window client DIPs.
inline constexpr std::array kOnboardingLandingElements{
    Element::kAddress, Element::kSecurityChip, Element::kLibrary,
    Element::kSidebar, Element::kToolbar};
struct OnboardingLanding {
  Element element = Element::kAddress;
  Rect bounds;
  bool visible = false;
  friend bool operator==(const OnboardingLanding&,
                         const OnboardingLanding&) = default;
};
// One answer leaving the card for its home in the chrome.
struct OnboardingFlight {
  Element target = Element::kAddress;
  ImageId icon;
  std::uint32_t serial = 0;  // 0: none yet
};
// A theme change spreading from where it was made.
struct OnboardingReveal {
  Point origin;
  std::uint32_t serial = 0;
};

// Answers that wait for Next. Back keeps them; nothing here is sent until the
// moment commits.
struct OnboardingDraft {
  ImportSourceId import_source;
  Flags<ImportData> import_items = kAllImportData;
  SearchEngineId engine;
  bool make_default = false;
};

struct OnboardingState {
  bool default_requested = false;
  std::uint64_t revision = 0;
  [[nodiscard]] const ImportSource* FindImportSource(
      ImportSourceId id) const noexcept;
  [[nodiscard]] Result<void> UpsertImportSource(ImportSource value);
  [[nodiscard]] Result<void> RemoveImportSource(ImportSourceId id);
  [[nodiscard]] Result<void> SetImportProgress(const ImportProgress& value);
  // Records a chrome anchor; true when a landing target changed.
  bool SetLanding(const Anchor& anchor) noexcept;
  [[nodiscard]] const OnboardingLanding* Landing(
      Element element) const noexcept;

  bool open = false;
  bool first_run = false;
  OnboardingStep step = OnboardingStep::kIgnition;
  std::int8_t direction = 1;  // +1 forward, -1 back
  std::uint32_t open_serial = 0;
  std::uint32_t step_serial = 0;
  OnboardingCapabilities capabilities;
  OnboardingDraft draft;

  // Shape: the layout being previewed over the real window, and the veil
  // that hides the chrome while it changes underneath.
  std::optional<LayoutPrefs> ghost;
  bool chrome_veiled = false;
  std::uint32_t morph_serial = 0;

  // Color: a local draft that the whole window previews while editing.
  bool theme_ready = false, theme_editing = false, theme_edited = false;
  SpaceId theme_space;
  WorkspaceTheme theme, theme_before;
  OnboardingReveal reveal;

  std::array<OnboardingLanding, kOnboardingLandingElements.size()> landings{};
  OnboardingFlight flight;

  // Host-published records, valid while onboarding is visible.
  std::vector<ImportSource> import_sources;
  ImportProgress import;
  bool import_requested = false;

  std::uint32_t committed = 0;  // bit per OnboardingStep
  bool finishing = false, reported = false;
  OnboardingOutcome outcome = OnboardingOutcome::kCompleted;
};
}  // namespace kr
