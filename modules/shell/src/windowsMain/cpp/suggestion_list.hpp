#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "view_context.hpp"

namespace kr::win {

class SuggestionList {
 public:
  explicit SuggestionList(ViewContext& context);
  ~SuggestionList();
  void Render(std::span<const SuggestionVm> suggestions,
              int selected,
              const ImageStore* images);
  void ClearImages();
  [[nodiscard]] const controls::ListView& Root() const noexcept {
    return list_;
  }

 private:
  struct Row;
  struct Record {
    std::string contents, description;
    winrt::hstring title, detail;
    SuggestionKind kind = SuggestionKind::kUrl;
    media::ImageSource bitmap{nullptr};
    IInspectable item{nullptr};
    bool selected = false;
    bool rich_image = false;
    std::vector<SuggestionClassification> contents_class, description_class;
    std::uint64_t text_revision = 0;
  };
  void UpdateRow(Row& row);
  ViewContext& context_;
  controls::ListView list_;
  std::vector<Record> records_;
  std::uint64_t text_revision_ = 0;
  std::unordered_map<void*, std::unique_ptr<Row>> rows_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::ListViewBase::ItemClick_revoker clicked_;
  xaml::UIElement::PointerReleased_revoker pointer_released_;
  controls::ListViewBase::ContainerContentChanging_revoker containers_;
  foundation::TypedEventHandler<controls::ListViewBase,
                                controls::ContainerContentChangingEventArgs>
      realize_{nullptr};
};

}  // namespace kr::win
