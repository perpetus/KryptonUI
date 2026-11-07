#include "krypton/ui/model/mnemonic.hpp"

#include "krypton/testing/test.hpp"

namespace kr {
namespace {

KR_TEST(MnemonicMarkerIsRemovedAndNamesTheAccessKey) {
  const auto label = ParseMnemonic("New &tab");
  KR_EXPECT_EQ(label.text, std::string("New tab"));
  KR_EXPECT_EQ(label.access_key, std::string("t"));
}

KR_TEST(MnemonicDoubleAmpersandIsLiteral) {
  const auto label = ParseMnemonic("Bookmarks && &lists");
  KR_EXPECT_EQ(label.text, std::string("Bookmarks & lists"));
  KR_EXPECT_EQ(label.access_key, std::string("l"));
}

KR_TEST(MnemonicKeepsOnlyTheFirstKey) {
  const auto label = ParseMnemonic("&Find and &edit");
  KR_EXPECT_EQ(label.text, std::string("Find and edit"));
  KR_EXPECT_EQ(label.access_key, std::string("F"));
}

KR_TEST(MnemonicKeyMayBeMultibyte) {
  const auto label = ParseMnemonic("Nouvel &\xC3\xA9tat");
  KR_EXPECT_EQ(label.text, std::string("Nouvel \xC3\xA9tat"));
  KR_EXPECT_EQ(label.access_key, std::string("\xC3\xA9"));
}

KR_TEST(MnemonicIgnoresTrailingMarkerAndSpaceKey) {
  KR_EXPECT_EQ(ParseMnemonic("Exit&").text, std::string("Exit"));
  const auto spaced = ParseMnemonic("A & B");
  KR_EXPECT_EQ(spaced.text, std::string("A  B"));
  KR_EXPECT(spaced.access_key.empty());
  KR_EXPECT(ParseMnemonic("Plain").access_key.empty());
}

}  // namespace
}  // namespace kr
