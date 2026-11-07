#include <algorithm>
#include <format>
#include <utility>

#include "krypton/ui/presentation/presenter.hpp"

namespace kr {
namespace {
template <typename T>
int Compare(T a, T b) {
  return a < b ? -1 : a > b ? 1 : 0;
}
void Bytes(std::string& output, std::uint64_t value, bool rate) {
  output.clear();
  if (value >= 1024 * 1024)
    std::format_to(std::back_inserter(output), "{:.1f} MiB{}",
                   static_cast<double>(value) / (1024 * 1024),
                   rate ? "/s" : "");
  else if (value >= 1024)
    std::format_to(std::back_inserter(output), "{:.1f} KiB{}",
                   static_cast<double>(value) / 1024, rate ? "/s" : "");
  else
    std::format_to(std::back_inserter(output), "{} B{}", value,
                   rate ? "/s" : "");
}
}  // namespace
void Presenter::PresentBrowserTools(const BrowserToolsState& tools) {
  if (task_revision_ != tools.task_revision) {
    task_order_.clear();
    for (const auto& task : tools.tasks) {
      if (!TaskVisible(task, tools.task_filter, tools.task_search))
        continue;
      task_order_.push_back(&task);
    }
    std::ranges::sort(task_order_,
                      [&](const BrowserTask* a, const BrowserTask* b) {
                        int value = 0;
                        switch (tools.task_sort) {
                          case TaskColumn::kName:
                            value = a->title.compare(b->title);
                            break;
                          case TaskColumn::kMemory:
                            value = Compare(a->memory_bytes, b->memory_bytes);
                            break;
                          case TaskColumn::kCpu:
                            value = Compare(a->cpu_percent, b->cpu_percent);
                            break;
                          case TaskColumn::kNetwork:
                            value = Compare(a->network_bytes_per_second,
                                            b->network_bytes_per_second);
                            break;
                          case TaskColumn::kProcess:
                            value = Compare(a->process_id, b->process_id);
                            break;
                        }
                        if (!value)
                          return a->id < b->id;
                        return tools.ascending ? value < 0 : value > 0;
                      });
    task_text_.resize(task_order_.size());
    task_rows_.clear();
    task_rows_.reserve(task_order_.size());
    for (std::size_t i = 0; i < task_order_.size(); ++i) {
      const auto& task = *task_order_[i];
      auto& text = task_text_[i];
      Bytes(text.memory, task.memory_bytes, false);
      Bytes(text.network, task.network_bytes_per_second, true);
      text.cpu.clear();
      text.process.clear();
      if (task.cpu_percent < 0)
        text.cpu = "\xE2\x80\x94";
      else
        std::format_to(std::back_inserter(text.cpu), "{:.1f}%",
                       task.cpu_percent);
      std::format_to(std::back_inserter(text.process), "{}", task.process_id);
      task_rows_.push_back(
          {task.id, task.title, task.profile, text.memory, text.cpu,
           text.network, text.process, task.icon, task.category,
           std::ranges::binary_search(tools.selected_tasks, task.id)});
    }
    task_status_.clear();
    std::format_to(std::back_inserter(task_status_), "{} of {} tasks",
                   task_rows_.size(), tools.tasks.size());
    if (!task_order_.empty() && !task_order_.front()->profile.empty() &&
        std::ranges::all_of(task_order_, [&](const BrowserTask* task) {
          return task->profile == task_order_.front()->profile;
        })) {
      task_status_ += " · ";
      task_status_ += task_order_.front()->profile;
    }
    task_revision_ = tools.task_revision;
  }
  if (permission_revision_ != tools.permission_revision) {
    permission_rows_.clear();
    for (const auto& item : tools.permissions) {
      if (!tools.permission_origin.empty() &&
          item.origin != tools.permission_origin)
        continue;
      if (!ToolSearchMatches(item.label, tools.permission_search) &&
          !ToolSearchMatches(item.origin, tools.permission_search) &&
          !ToolSearchMatches(item.description, tools.permission_search))
        continue;
      SitePermissionVm row{item.id,          item.origin, item.label,
                           item.description, item.kind,   item.setting};
      row.editable = tools.can_edit_permissions && !item.managed;
      row.managed = item.managed;
      for (std::uint32_t setting = 0; setting < kPermissionSettingLabels.size();
           ++setting)
        if (PermissionSettingAllowed(item,
                                     static_cast<PermissionSetting>(setting)))
          row.choices[row.choice_count++] =
              static_cast<PermissionSetting>(setting);
      if (std::ranges::find(std::span(row.choices).first(row.choice_count),
                            item.setting) ==
          std::span(row.choices).first(row.choice_count).end())
        row.choices[row.choice_count++] = item.setting;
      permission_rows_.push_back(row);
    }
    std::ranges::sort(permission_rows_,
                      [](const SitePermissionVm& a, const SitePermissionVm& b) {
                        if (a.origin != b.origin)
                          return a.origin < b.origin;
                        if (a.label != b.label)
                          return a.label < b.label;
                        return a.id < b.id;
                      });
    permission_status_ = std::format("{} permissions", permission_rows_.size());
    permission_revision_ = tools.permission_revision;
  }
  vm_.browser_tools = {
      tools.tasks_open,
      tools.permissions_open,
      tools.CanEndSelection(),
      tools.can_reset_permissions && !tools.permission_origin.empty(),
      tools.task_serial,
      tools.permission_serial,
      tools.task_revision,
      tools.permission_revision,
      tools.prompt_revision,
      tools.task_filter,
      tools.task_sort,
      tools.ascending,
      tools.task_search,
      tools.permission_search,
      tools.permission_origin,
      task_status_,
      permission_status_,
      tools.task_error,
      tools.permission_error,
      task_rows_,
      permission_rows_,
      tools.prompt ? &*tools.prompt : nullptr};
}
}  // namespace kr
