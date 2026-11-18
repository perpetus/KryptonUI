#include "settings_abi.hpp"

#include <utility>

#include "krypton/ui/shell/shell.hpp"
namespace kr::abi {
namespace {
bool Valid(KrStr s) noexcept {
  return s.data || !s.size;
}
std::string Copy(KrStr s) {
  return s.data ? std::string(s.data, s.size) : std::string{};
}
template <class F>
KrResult Call(F&& function) noexcept {
  try {
    return function() ? KR_OK : KR_ERR_INVALID_ARGUMENT;
  } catch (...) {
    return KR_ERR_PLATFORM;
  }
}

bool ValidModel(const KrLocalModel& row) noexcept {
  return row.kind <= KR_LOCAL_MODEL_VOICE && row.progress >= -1 &&
         row.progress <= 100 && row.name.size <= 256 && row.error.size <= 512 &&
         Valid(row.name) && Valid(row.error);
}

LocalModelRow ReadModel(const KrLocalModel& row) {
  return {row.id,
          static_cast<LocalModelKind>(row.kind),
          Flags<LocalModelFlag>::FromBits(row.flags),
          row.progress,
          row.download_bytes,
          Copy(row.name),
          Copy(row.error),
          std::nullopt};
}

bool ValidChoiceLabel(KrStr value, size_t limit) noexcept {
  return value.data && value.size && value.size <= limit;
}
}  // namespace
void FillSettingsUi(KrShellUI& ui) {
  ui.set_suggestion_selection = [](void* self, uint32_t request,
                                   uint64_t generation, size_t index) noexcept {
    return Call([&] {
      return static_cast<Shell*>(self)->SetSuggestionSelection(
          request, generation, index);
    });
  };
  ui.show_notification = [](void* self, KrStr text) noexcept {
    if (!Valid(text))
      return KR_ERR_INVALID_ARGUMENT;
    return Call([&] {
      return static_cast<Shell*>(self)->ShowNotification(
          text.data ? std::string_view(text.data, text.size)
                    : std::string_view{});
    });
  };
  ui.set_settings_snapshot = [](void* self,
                                const KrSettingsSnapshot* value) noexcept {
    if (!value || !KR_HAS_FIELD(value, KrSettingsSnapshot, build) ||
        !Valid(value->profile_name) || !Valid(value->download_location) ||
        !Valid(value->product) || !Valid(value->version) ||
        !Valid(value->build))
      return KR_ERR_INVALID_ARGUMENT;
    return Call([&] {
      return static_cast<Shell*>(self)->SetBrowserSettings(
          {value->revision, value->controls, value->managed, value->actions,
           value->startup, value->ask_download_location,
           Copy(value->profile_name), Copy(value->download_location),
           Copy(value->product), Copy(value->version), Copy(value->build)});
    });
  };
  ui.set_local_models = [](void* self,
                           const KrLocalModelsSnapshot* value) noexcept {
    if (!self || !KR_HAS_FIELD(value, KrLocalModelsSnapshot, checking) ||
        (value->count && !value->rows) || value->count > 128 ||
        value->row_size != sizeof(KrLocalModel))
      return KR_ERR_INVALID_ARGUMENT;
    return Call([&]() -> Result<void> {
      LocalModelsState next;
      next.checking = value->checking;
      next.rows.reserve(value->count);
      for (size_t i = 0; i < value->count; ++i) {
        const auto& row = value->rows[i];
        if (!ValidModel(row))
          return Fail(ErrorCode::kInvalidArgument, "Invalid local model row");
        next.rows.push_back(ReadModel(row));
      }
      return static_cast<Shell*>(self)->SetLocalModels(std::move(next));
    });
  };
  ui.set_local_models_v2 = [](void* self,
                              const KrLocalModelsSnapshotV2* value) noexcept {
    if (!self || !KR_HAS_FIELD(value, KrLocalModelsSnapshotV2, checking) ||
        (value->count && !value->rows) || value->count > 128 ||
        value->row_size != sizeof(KrLocalModelWithVoiceVariant))
      return KR_ERR_INVALID_ARGUMENT;
    return Call([&]() -> Result<void> {
      LocalModelsState next;
      next.checking = value->checking;
      next.rows.reserve(value->count);
      for (size_t i = 0; i < value->count; ++i) {
        const auto& row = value->rows[i];
        if (!ValidModel(row.model))
          return Fail(ErrorCode::kInvalidArgument, "Invalid local model row");
        auto model = ReadModel(row.model);
        if (model.kind == LocalModelKind::kVoice) {
          if (!ValidChoiceLabel(row.language_tag, 32) ||
              !ValidChoiceLabel(row.language_name, 256) ||
              !ValidChoiceLabel(row.model_family, 128) ||
              row.voice_size < KR_VOICE_MODEL_TINY ||
              row.voice_size > KR_VOICE_MODEL_MEDIUM)
            return Fail(ErrorCode::kInvalidArgument, "Invalid voice variant");
          model.voice =
              VoiceModelVariant{Copy(row.language_tag), Copy(row.language_name),
                                Copy(row.model_family),
                                static_cast<VoiceModelSize>(row.voice_size)};
        }
        next.rows.push_back(std::move(model));
      }
      return static_cast<Shell*>(self)->SetLocalModels(std::move(next));
    });
  };
  ui.settings_operation_result =
      [](void* self, const KrSettingsOperationResult* value) noexcept {
        if (!value || !KR_HAS_FIELD(value, KrSettingsOperationResult, result) ||
            value->result < KR_OK || value->result > KR_ERR_PLATFORM)
          return KR_ERR_INVALID_ARGUMENT;
        return Call([&] {
          return static_cast<Shell*>(self)->CompleteSettingsOperation(
              value->request_id, value->result == KR_OK);
        });
      };
}
static_assert(std::to_underlying(SettingsSection::kWorkspaces) ==
              KR_SETTINGS_WORKSPACES);
static_assert(std::to_underlying(SettingsAction::kBrowserSettings) ==
              KR_SETTINGS_ACTION_BROWSER_SETTINGS);
static_assert(std::to_underlying(SettingsAction::kLocalModels) ==
              KR_SETTINGS_ACTION_LOCAL_MODELS);
static_assert(std::to_underlying(SettingsControl::kSearch) ==
              KR_SETTINGS_SEARCH_ENGINE);
static_assert(std::to_underlying(VoiceModelSize::kTiny) == KR_VOICE_MODEL_TINY);
static_assert(std::to_underlying(VoiceModelSize::kSmall) ==
              KR_VOICE_MODEL_SMALL);
static_assert(std::to_underlying(VoiceModelSize::kMedium) ==
              KR_VOICE_MODEL_MEDIUM);
}  // namespace kr::abi
