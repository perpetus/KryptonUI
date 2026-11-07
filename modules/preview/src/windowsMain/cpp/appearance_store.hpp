#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::preview {

// The preview owns its preferences; the shell continues to receive effective
// appearance through the C ABI. All filesystem work runs on one sleeping
// worker.
class AppearanceStore final {
 public:
  using Delivery =
      std::function<void(std::optional<fixtures::AppearancePrefs>)>;

  // Starts one asynchronous read. Delivery runs exactly once on the worker,
  // outside its mutex; nullopt means the file was missing or invalid. A UI
  // callback must post through its own lifetime guard rather than touch a view.
  AppearanceStore(std::filesystem::path path, Delivery loaded);
  // Drains pending writes and completions, then joins. Destroy after the UI
  // loop.
  ~AppearanceStore();

  AppearanceStore(const AppearanceStore&) = delete;
  AppearanceStore& operator=(const AppearanceStore&) = delete;
  AppearanceStore(AppearanceStore&&) = delete;
  AppearanceStore& operator=(AppearanceStore&&) = delete;

  // Replaces the pending snapshot in memory; never performs filesystem work.
  // Invalid snapshots are ignored. Writes are best effort and replace the file
  // atomically, preserving its previous contents if replacement fails.
  void Save(fixtures::AppearancePrefs prefs);

  // Posts a write barrier without waiting on the caller's thread. Completion
  // runs on the worker, outside its mutex, after all earlier Save calls have
  // been attempted. It does not imply that filesystem writes succeeded.
  void FlushAsync(std::function<void()> completion);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kr::preview
