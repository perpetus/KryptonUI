#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "library_visuals.hpp"

namespace kr::win {

// A library section's rows (history, boosts, downloads, spaces) or its media
// grid. WinUI virtualizes the containers; only realized rows hold controls,
// and recycled rows keep theirs. Records own copies of the borrowed view
// model so rows can realize between renders.
class LibraryList {
 public:
  using Bounds = std::function<Rect(const xaml::FrameworkElement&)>;
  LibraryList(ViewContext& context, bool tiles, Bounds bounds);
  ~LibraryList();
  LibraryList(const LibraryList&) = delete;
  LibraryList& operator=(const LibraryList&) = delete;

  void Render(std::span<const LibraryRowVm> rows,
              LibrarySection section,
              const LibraryPaletteVm& palette,
              const ImageStore* images,
              UiTextView text);
  // Fits the media grid's square tiles to the content width.
  void SetWidth(double width);
  [[nodiscard]] const controls::ListViewBase& Root() const noexcept {
    return list_;
  }

 private:
  struct RecordKey {
    LibraryRowKind kind;
    std::uint64_t id;
    friend bool operator==(const RecordKey&, const RecordKey&) = default;
  };
  struct RecordKeyHash {
    [[nodiscard]] std::size_t operator()(RecordKey key) const noexcept;
  };
  struct Record {
    LibraryRowKind kind = LibraryRowKind::kItem;
    std::uint64_t token = 0, seen = 0;
    IInspectable native_item{nullptr};
    std::string title_text, subtitle_text, glyph_text;
    winrt::hstring title, subtitle, glyph;
    std::string file_name, status;  // downloads
    DownloadVm download;
    ImageId image;
    Color color;
    LibraryItemId item;
    SpaceId space;
    TabId tab;
    bool active = false, video = false, file = false, enabled = false;
  };
  struct Row;
  [[nodiscard]] static RecordKey KeyFor(const LibraryRowVm& row) noexcept;
  [[nodiscard]] Record* FindRecord(std::uint64_t token) noexcept;
  void UpdateRow(Row& row);
  void Activate(const Record& record);

  ViewContext& context_;
  bool tiles_;
  Bounds bounds_;
  controls::ListViewBase list_{nullptr};
  foundation::Collections::IObservableVector<IInspectable> items_{nullptr};
  std::unordered_map<RecordKey, Record, RecordKeyHash> records_;
  std::unordered_map<std::uint64_t, Record*> tokens_;
  std::vector<std::uint64_t> desired_, displayed_;
  std::uint64_t next_token_ = 0, generation_ = 0;
  std::unordered_map<void*, std::unique_ptr<Row>> rows_;
  LibrarySection section_ = LibrarySection::kHistory;
  LibraryPaletteVm palette_;
  const ImageStore* images_ = nullptr;
  UiTextView text_;
  double tile_ = 0;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::ListViewBase::ItemClick_revoker clicked_;
  controls::ListViewBase::ContainerContentChanging_revoker containers_;
  foundation::TypedEventHandler<controls::ListViewBase,
                                controls::ContainerContentChangingEventArgs>
      realize_{nullptr};
};
}  // namespace kr::win
