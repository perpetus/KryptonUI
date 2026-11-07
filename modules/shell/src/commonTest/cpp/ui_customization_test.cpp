#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/presentation/favorite_layout.hpp"
#include "krypton/ui/presentation/presenter.hpp"

namespace kr {
namespace {

KR_TEST(UiTextCopiesHostStringsAndRestoresDefaults) {
  Store store;
  (void)store.TakeDirty();
  std::string translated = "Neuen Tab in diesem Arbeitsbereich öffnen";
  KR_ASSERT(store.SetUiText("new_tab", translated).has_value());
  translated.assign("changed by host");
  KR_EXPECT_EQ(store.text()[UiText::kNewTab],
               std::string_view("Neuen Tab in diesem Arbeitsbereich öffnen"));
  KR_EXPECT(store.TakeDirty().Has(Dirty::kText));
  KR_ASSERT(
      store.SetUiText("new_tab", store.text()[UiText::kNewTab]).has_value());
  KR_EXPECT(store.dirty().none());
  KR_EXPECT(!store.SetUiText("unknown_key", "text").has_value());
  KR_EXPECT(store.dirty().none());
  KR_ASSERT(store.SetUiText("new_tab", "").has_value());
  KR_EXPECT_EQ(store.text()[UiText::kNewTab], UiTextView{}[UiText::kNewTab]);
}

KR_TEST(PresenterBorrowsTranslatedTextAndWritingDirection) {
  Store store;
  KR_ASSERT(store.SetUiText("address", "ابحث أو أدخل عنوانًا").has_value());
  store.SetTextDirection(true);
  Presenter presenter;
  const auto& vm = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT_EQ(vm.text[UiText::kAddress],
               std::string_view("ابحث أو أدخل عنوانًا"));
  KR_EXPECT(vm.text.right_to_left);
  KR_EXPECT(vm.changed.Has(Dirty::kText));
  store.SetTextDirection(true);
  KR_EXPECT(store.dirty().none());
  store.SetTextDirection(false);
  KR_EXPECT(store.dirty().Has(Dirty::kText));
}

KR_TEST(FavoriteGridAdaptsToAvailableWidthAndCustomTokens) {
  KR_EXPECT_EQ(FavoriteColumns(0, 0), 0u);
  KR_EXPECT_EQ(FavoriteColumns(0, 4), 1u);
  KR_EXPECT_EQ(FavoriteColumns(std::numeric_limits<float>::quiet_NaN(), 4), 1u);
  KR_EXPECT_EQ(FavoriteColumns(176, 4), 3u);
  KR_EXPECT_EQ(FavoriteColumns(238, 4), 4u);
  KR_EXPECT_EQ(FavoriteColumns(238, 2), 2u);
  auto tokens = design::kTokens;
  tokens.spacing.favorite_height = 80;
  KR_EXPECT_EQ(FavoriteColumns(238, 4, tokens), 2u);
}

}  // namespace
}  // namespace kr
