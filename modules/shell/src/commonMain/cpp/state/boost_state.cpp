#include <utility>

#include "krypton/ui/state/store.hpp"

namespace kr {
void Store::OpenBoostEditor(BoostDraft value) {
  auto& editor = ui_.boost_editor;
  const bool resume = editor.open && editor.draft.id == value.id &&
                      editor.draft.tab == value.tab &&
                      editor.draft.revision == value.revision;
  value.serial = editor.draft.serial + (resume ? 0 : 1);
  editor = {true, false, std::move(value)};
  MarkDirty(Dirty::kWorkspaceEditor);
}
void Store::UpdateBoostEditor(BoostDraft value, bool picking) {
  ui_.boost_editor.draft = std::move(value);
  ui_.boost_editor.picking = picking;
  MarkDirty(Dirty::kWorkspaceEditor);
}
void Store::CloseBoostEditor() {
  ui_.boost_editor.open = false;
  ui_.boost_editor.picking = false;
  MarkDirty(Dirty::kWorkspaceEditor);
}
}  // namespace kr
