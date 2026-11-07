#include "krypton/ui/presentation/menu_shortcut.hpp"

#include "krypton/testing/test.hpp"

namespace kr {
namespace {
KR_TEST(MenuShortcutUsesMacSymbolsWithoutChangingSourceText) {
  const std::string source = "Ctrl+T";
  KR_EXPECT_EQ(MenuShortcutSymbols(source), std::string("\u2318\u2009T"));
  KR_EXPECT_EQ(source, std::string("Ctrl+T"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Alt+Shift+N"),
               std::string("\u2325\u21E7\u2009N"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+Shift+Alt+I"),
               std::string("\u2325\u21E7\u2318\u2009I"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Command+Shift+P"),
               std::string("\u21E7\u2318\u2009P"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Win+Left"),
               std::string("\u229E\u2009\u2190"));
}

KR_TEST(MenuShortcutPreservesZoomKeysFunctionKeysAndUnicode) {
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl++"), std::string("\u2318\u2009+"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+-"), std::string("\u2318\u2009-"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+="), std::string("\u2318\u2009="));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+Num +"),
               std::string("\u2318\u2009Num +"));
  KR_EXPECT_EQ(MenuShortcutSymbols("F11"), std::string("F11"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Shift+F5"), std::string("\u21E7\u2009F5"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+\u00D6"),
               std::string("\u2318\u2009\u00D6"));
}

KR_TEST(MenuShortcutUsesNavigationSymbolsAndKeepsUnknownBindings) {
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+Shift+Delete"),
               std::string("\u21E7\u2318\u2009\u2326"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Alt+Left"),
               std::string("\u2325\u2009\u2190"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+PgDn"),
               std::string("\u2318\u2009\u21DF"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Escape"), std::string("\u238B"));
  KR_EXPECT_EQ(MenuShortcutSymbols(""), std::string());
  KR_EXPECT_EQ(MenuShortcutSymbols("AltGr+Q"), std::string("AltGr+Q"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Strg+Umschalt+P"),
               std::string("Strg+Umschalt+P"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+AltGr+Q"),
               std::string("Ctrl+AltGr+Q"));
  KR_EXPECT_EQ(MenuShortcutSymbols("Ctrl+"), std::string("Ctrl+"));
}
}  // namespace
}  // namespace kr
