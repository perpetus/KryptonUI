#include "krypton/ui/presentation/onboarding_icons.hpp"

#include "krypton/ui/design/onboarding_icons.generated.hpp"

namespace kr {
namespace {
namespace icons = design::onboarding_icons;

constexpr char Lower(char value) noexcept {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                      : value;
}

bool Equal(std::string_view first, std::string_view second) noexcept {
  if (first.size() != second.size())
    return false;
  for (std::size_t i = 0; i < first.size(); ++i)
    if (Lower(first[i]) != Lower(second[i]))
      return false;
  return true;
}

bool BrowserPrefix(std::string_view name, std::string_view brand) noexcept {
  if (name.size() < brand.size() || !Equal(name.substr(0, brand.size()), brand))
    return false;
  const auto rest = name.substr(brand.size());
  return rest.empty() || rest.front() == ' ' || rest.front() == '\t' ||
         rest.front() == '(' || rest.front() == ':';
}

std::string_view Domain(std::string_view keyword) noexcept {
  if (keyword.starts_with('@'))
    keyword.remove_prefix(1);
  for (const auto prefix :
       {std::string_view("https://"), std::string_view("http://"),
        std::string_view("www.")})
    if (keyword.size() >= prefix.size() &&
        Equal(keyword.substr(0, prefix.size()), prefix))
      keyword.remove_prefix(prefix.size());
  return keyword.substr(0, keyword.find_first_of("/:?#"));
}
}  // namespace

const OnboardingIcon* OnboardingBrowserIcon(std::string_view name) noexcept {
  const OnboardingIcon* result = nullptr;
  std::size_t longest = 0;
  for (const auto& alias : icons::kBrowsers)
    if (alias.text.size() > longest && BrowserPrefix(name, alias.text)) {
      result = alias.icon;
      longest = alias.text.size();
    }
  return result;
}

const OnboardingIcon* OnboardingSearchIcon(std::string_view keyword,
                                           std::string_view name) noexcept {
  const auto domain = Domain(keyword);
  for (const auto& alias : icons::kDomains) {
    if (Equal(domain, alias.text) ||
        (domain.size() > alias.text.size() &&
         domain[domain.size() - alias.text.size() - 1] == '.' &&
         Equal(domain.substr(domain.size() - alias.text.size()), alias.text)))
      return alias.icon;
  }
  for (const auto& alias : icons::kSearches)
    if (Equal(name, alias.text))
      return alias.icon;
  return nullptr;
}
}  // namespace kr
