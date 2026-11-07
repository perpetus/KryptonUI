#include "menu_presenter.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <limits>
#include <utility>

#include "app_menu.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/menu_shortcut.hpp"
#include "menu_item_style.hpp"
#include "native_input.hpp"

namespace kr::win {
using namespace controls;
using winrt::Windows::System::VirtualKey;
using xaml::CornerRadius;
using xaml::Thickness;
using xaml::Visibility;
namespace hosting = xaml::Hosting;

namespace {
// The native element used for a row.
enum class NativeRow { kSeparator, kSubmenu, kCommand, kToggle, kRadio };

NativeRow RowFor(const MenuItem& item) {
  if (item.type == MenuItemType::kSeparator)
    return NativeRow::kSeparator;
  if ((item.type == MenuItemType::kSubmenu ||
       item.type == MenuItemType::kHeader ||
       item.type == MenuItemType::kControls) &&
      item.submenu >= 0)
    return NativeRow::kSubmenu;
  if (item.type == MenuItemType::kCheck ||
      (item.type == MenuItemType::kCommand && item.checked))
    return NativeRow::kToggle;
  if (item.type == MenuItemType::kRadio)
    return NativeRow::kRadio;
  return NativeRow::kCommand;
}

bool SameItem(const MenuItem& previous, const MenuItem& next) {
  if (RowFor(previous) != RowFor(next))
    return false;
  // A Chromium republication can renumber transport commands. Source command
  // ids identify the surviving native row, while its handler uses the newest
  // transport id. Separators have no command and are matched in order.
  if (RowFor(next) == NativeRow::kSeparator)
    return true;
  if (previous.metadata && next.metadata)
    return previous.metadata->source_type == next.metadata->source_type &&
           previous.metadata->source_command_id ==
               next.metadata->source_command_id &&
           previous.metadata->element_identifier ==
               next.metadata->element_identifier;
  return !previous.metadata && !next.metadata &&
         previous.command_id == next.command_id;
}

template <typename Item>
void SetIcon(const Item& native,
             const MenuItem& item,
             const media::ImageSource& bitmap) {
  const auto current = native.Icon();
  if (!item.glyph.empty()) {
    if (const auto icon = current.template try_as<FontIcon>()) {
      const auto glyph = winrt::to_hstring(item.glyph);
      if (icon.Glyph() != glyph)
        icon.Glyph(glyph);
    } else {
      FontIcon replacement;
      replacement.FontFamily(
          media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
      replacement.Glyph(winrt::to_hstring(item.glyph));
      native.Icon(replacement);
    }
  } else if (bitmap) {
    if (const auto icon = current.template try_as<ImageIcon>()) {
      if (icon.Source() != bitmap)
        icon.Source(bitmap);
    } else {
      ImageIcon replacement;
      replacement.Source(bitmap);
      native.Icon(replacement);
    }
  } else if (current) {
    native.Icon(nullptr);
  }
}

// Lends a reply's images to the rows only while they are built.
struct MenuImagesScope {
  const ImageStore*& images;
  ~MenuImagesScope() { images = nullptr; }
};

MenuFlyoutPresenter FindMenuPresenter(const xaml::DependencyObject& root,
                                      unsigned depth = 0) {
  if (!root || depth > 32)
    return nullptr;
  if (const auto presenter = root.try_as<MenuFlyoutPresenter>())
    return presenter;
  const auto count = media::VisualTreeHelper::GetChildrenCount(root);
  for (int index = 0; index < count; ++index) {
    if (const auto presenter = FindMenuPresenter(
            media::VisualTreeHelper::GetChild(root, index), depth + 1))
      return presenter;
  }
  return nullptr;
}
}  // namespace

struct MenuPresenter::NativeItem {
  MenuItem item;
  MenuFlyoutItemBase element{nullptr};
  std::vector<std::unique_ptr<NativeItem>> children;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker key;
  MenuFlyoutItem::Click_revoker click;
};

MenuPresenter::MenuPresenter(HWND parent, ViewContext& context, Task on_closed)
    : context_(context), parent_(parent), on_closed_(std::move(on_closed)) {}

bool MenuPresenter::IsOpen() const noexcept {
  return menu_open_ || (app_menu_ && app_menu_->IsOpen());
}
bool MenuPresenter::Contains(Point point) const noexcept try {
  if (app_menu_ && app_menu_->IsOpen())
    return app_menu_->Contains(point);
  if (!menu_open_ || !menu_owner_ || !menu_owner_.XamlRoot())
    return false;
  for (const auto& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(
           menu_owner_.XamlRoot())) {
    const auto child = popup.Child().try_as<xaml::FrameworkElement>();
    if (!child)
      continue;
    const auto bounds = child.TransformToVisual(nullptr).TransformBounds(
        {0, 0, static_cast<float>(child.ActualWidth()),
         static_cast<float>(child.ActualHeight())});
    if (Rect{bounds.X + owner_bounds_.x, bounds.Y + owner_bounds_.y,
             bounds.Width, bounds.Height}
            .Contains(point))
      return true;
  }
  return false;
} catch (...) {
  // A popup can be tearing down while the host routes the next input message.
  return false;
}

void MenuPresenter::SetText(UiTextView text) {
  back_text_ = NativeText(UiText::kBack, text);
  if (app_menu_)
    app_menu_->SetBackText(back_text_);
}

MenuPresenter::~MenuPresenter() {
  Close(false);
}

void MenuPresenter::Close(bool release) noexcept {
  const auto generation = ++menu_generation_;
  const auto request = menu_request_;
  ClearGlassMenus();
  if (app_menu_)
    app_menu_->Close(release);
  const bool notify = std::exchange(menu_open_, false);
  menu_opened_.revoke();
  menu_closed_.revoke();
  try {
    if (menu_)
      menu_.Hide();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Context menu could not close");
  }
  if (notify)
    try {
      context_.delegate.OnMenuClosed(request);
      if (release && generation == menu_generation_ && request == menu_request_)
        on_closed_();
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Context menu closure could not be reported");
    }
}

void MenuPresenter::Update(Rect viewport,
                           float scale,
                           xaml::ElementTheme theme,
                           xaml::FlowDirection direction) {
  if (app_menu_ && app_menu_->IsOpen())
    app_menu_->Update(viewport, scale, theme, direction);
  if (menu_ && menu_open_) {
    menu_.MenuFlyoutPresenterStyle(PresenterStyle(theme));
    menu_.SystemBackdrop(material_.For(context_.brushes.menu_backdrop_enabled(),
                                       context_.brushes.menu_material()));
    RefreshGlassMenus();
  }
}

void MenuPresenter::ClearGlassMenus() noexcept {
  menu_glass_layout_.revoke();
  try {
    for (const auto& presenter : glass_menus_)
      context_.glass.Place(presenter, {});
  } catch (...) {
  }
  glass_menus_.clear();
  seen_glass_menus_.clear();
}

void MenuPresenter::RefreshGlassMenus() noexcept {
  if (!menu_open_ || refreshing_glass_ || !menu_owner_ ||
      !menu_owner_.XamlRoot())
    return;
  refreshing_glass_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{refreshing_glass_};
  try {
    seen_glass_menus_.clear();
    for (const auto& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(
             menu_owner_.XamlRoot())) {
      const auto presenter = FindMenuPresenter(popup.Child());
      if (!presenter)
        continue;
      seen_glass_menus_.push_back(presenter);
      if (std::ranges::find(glass_menus_, presenter) == glass_menus_.end())
        glass_menus_.push_back(presenter);
      if (presenter.ActualWidth() <= 0 || presenter.ActualHeight() <= 0)
        continue;
      const auto rect = presenter.TransformToVisual(nullptr).TransformBounds(
          {0, 0, static_cast<float>(presenter.ActualWidth()),
           static_cast<float>(presenter.ActualHeight())});
      const auto brush = context_.glass.For(
          presenter, GlassRole::kMenu, context_.brushes.menu_background,
          static_cast<float>(presenter.CornerRadius().TopLeft));
      if (presenter.Background() != brush)
        presenter.Background(brush);
      context_.glass.Place(presenter,
                           {rect.X + owner_bounds_.x, rect.Y + owner_bounds_.y,
                            rect.Width, rect.Height});
    }
    std::erase_if(glass_menus_, [this](const auto& presenter) {
      if (std::ranges::find(seen_glass_menus_, presenter) !=
          seen_glass_menus_.end())
        return false;
      context_.glass.Place(presenter, {});
      return true;
    });
  } catch (...) {
    // Keep the native backdrop when the popup cannot map to client space.
  }
}
void MenuPresenter::Reanchor(Rect anchor) {
  if (kind_ == MenuKind::kApp && app_menu_ && app_menu_->IsOpen())
    app_menu_->Reanchor(anchor);
}
void MenuPresenter::Raise() noexcept {
  if (app_menu_ && app_menu_->IsOpen())
    app_menu_->Raise();
}

void MenuPresenter::Show(std::uint32_t request,
                         const MenuTree& tree,
                         Rect anchor,
                         const ImageStore& images,
                         MenuKind kind,
                         const ShellLayout& layout,
                         const Grid& sidebar,
                         const Grid& toolbar,
                         Rect viewport,
                         float scale) {
  if (tree.panel) {
    if (!app_menu_ || !app_menu_->IsOpen() || app_menu_->Request() != request)
      Close(false);
    if (!app_menu_)
      app_menu_ = std::make_unique<AppMenuPresenter>(parent_, context_,
                                                     [this] { on_closed_(); });
    app_menu_->SetBackText(back_text_);
    const bool on_toolbar = layout.toolbar_visible &&
                            (!layout.sidebar_visible ||
                             !layout.sidebar.Contains({anchor.x, anchor.y}));
    const auto& owner = on_toolbar ? toolbar : sidebar;
    kind_ = kind;
    app_menu_->Show(request, tree, anchor, images, viewport, scale,
                    owner.RequestedTheme(), owner.FlowDirection());
    return;
  }
  if (menu_open_ && menu_ && menu_request_ == request) {
    // Hosts republish an open request when source rows change, as the page
    // menu does once the link to its selected text is ready. Closing here
    // would end the request, so keep the open flyout and its position.
    kind_ = kind;
    RefreshMenu(request, tree, images);
    return;
  }
  Close(false);
  menu_request_ = request;
  kind_ = kind;
  ShowMenu(request, tree, anchor, images, kind, layout, sidebar, toolbar);
}

void MenuPresenter::InstallStyle(const xaml::FrameworkElement& root) {
  root.Resources().Insert(
      winrt::box_value(winrt::xaml_typename<MenuFlyoutPresenter>()),
      PresenterStyle(xaml::ElementTheme::Default));
}

xaml::Style MenuPresenter::PresenterStyle(xaml::ElementTheme theme) {
  auto& style = presenter_styles_[static_cast<std::size_t>(theme)];
  if (!style) {
    style = xaml::Style(winrt::xaml_typename<MenuFlyoutPresenter>());
    const auto setters = style.Setters();
    setters.Append(
        xaml::Setter(xaml::FrameworkElement::RequestedThemeProperty(),
                     winrt::box_value(theme)));
    setters.Append(
        xaml::Setter(Control::ForegroundProperty(), context_.brushes.text));
    setters.Append(xaml::Setter(Control::BackgroundProperty(),
                                context_.brushes.menu_background));
    setters.Append(xaml::Setter(Control::BorderBrushProperty(),
                                context_.brushes.menu_border));
    setters.Append(xaml::Setter(
        Control::BorderThicknessProperty(),
        winrt::box_value(
            Thickness{kTokens.menu.border_width, kTokens.menu.border_width,
                      kTokens.menu.border_width, kTokens.menu.border_width})));
    setters.Append(
        xaml::Setter(Control::CornerRadiusProperty(),
                     winrt::box_value(UniformCorners(kTokens.menu.radius))));
    setters.Append(xaml::Setter(
        Control::PaddingProperty(),
        winrt::box_value(Thickness{kTokens.menu.padding, kTokens.menu.padding,
                                   kTokens.menu.padding,
                                   kTokens.menu.padding})));
  }
  // Implicit styling also reaches native submenu presenters, as in the
  // recreation's application resource dictionary.
  return style;
}

void MenuPresenter::ShowMenu(std::uint32_t request_id,
                             const MenuTree& tree,
                             Rect anchor,
                             const ImageStore& images,
                             MenuKind kind,
                             const ShellLayout& layout,
                             const Grid& sidebar,
                             const Grid& toolbar) {
  menu_closed_.revoke();
  if (menu_)
    menu_.Hide();
  menu_rows_.clear();
  menu_ = MenuFlyout();
  // Constrained Flyout popups can suppress native system backdrop rendering.
  menu_.ShouldConstrainToRootBounds(false);
  menu_.SystemBackdrop(material_.For(context_.brushes.menu_backdrop_enabled(),
                                     context_.brushes.menu_material()));
  (void)kind;
  const bool on_toolbar = layout.toolbar_visible &&
                          (!layout.sidebar_visible ||
                           !layout.sidebar.Contains({anchor.x, anchor.y}));
  const auto& menu_root = on_toolbar ? toolbar : sidebar;
  menu_owner_ = menu_root;
  owner_bounds_ = on_toolbar ? layout.toolbar : layout.sidebar;
  menu_.MenuFlyoutPresenterStyle(PresenterStyle(menu_root.RequestedTheme()));
  images_ = &images;
  const MenuImagesScope scope{images_};
  if (!tree.menus.empty())
    UpdateMenu(tree, 0, menu_.Items(), menu_rows_, request_id);
  menu_open_ = true;
  menu_glass_layout_ = menu_owner_.LayoutUpdated(
      winrt::auto_revoke, [this](auto&&, auto&&) { RefreshGlassMenus(); });
  menu_opened_ = menu_.Opened(winrt::auto_revoke,
                              [this](auto&&, auto&&) { RefreshGlassMenus(); });
  const auto generation = ++menu_generation_;
  menu_closed_ = menu_.Closed(
      winrt::auto_revoke, [this, request_id, generation](auto&&, auto&&) {
        if (generation != menu_generation_ || request_id != menu_request_)
          return;
        menu_open_ = false;
        ClearGlassMenus();
        context_.delegate.OnMenuClosed(request_id);
        if (generation == menu_generation_ && request_id == menu_request_)
          on_closed_();
      });
  const auto menu_bounds = on_toolbar ? layout.toolbar : layout.sidebar;
  xaml::Controls::Primitives::FlyoutShowOptions options;
  options.Position(winrt::box_value(foundation::Point{anchor.x - menu_bounds.x,
                                                      anchor.y + anchor.height -
                                                          menu_bounds.y})
                       .as<foundation::IReference<foundation::Point>>());
  menu_.ShowAt(menu_root, options);
}

std::unique_ptr<MenuPresenter::NativeItem> MenuPresenter::CreateItem(
    const MenuItem& item,
    std::uint32_t request_id) {
  auto row = std::make_unique<NativeItem>();
  row->item = item;
  const auto kind = RowFor(item);
  if (kind == NativeRow::kSeparator) {
    row->element = MenuFlyoutSeparator();
    return row;
  }
  Control control{nullptr};
  if (kind == NativeRow::kSubmenu) {
    MenuFlyoutSubItem submenu;
    row->element = submenu;
    control = submenu;
  } else {
    MenuFlyoutItem native{nullptr};
    if (kind == NativeRow::kToggle)
      native = ToggleMenuFlyoutItem();
    else if (kind == NativeRow::kRadio)
      native = RadioMenuFlyoutItem();
    else
      native = MenuFlyoutItem();
    row->element = native;
    control = native;
    row->click = native.Click(winrt::auto_revoke, [this, state = row.get(),
                                                   request_id](auto&&, auto&&) {
      if (!menu_open_ || menu_request_ != request_id)
        return;
      const auto command = state->item.command_id;
      const bool keep = state->item.keep_open;
      context_.delegate.OnMenuCommand(request_id, command);
      if (!keep && menu_open_ && menu_request_ == request_id)
        Close();
    });
  }
  StyleItem(control, kind == NativeRow::kSubmenu);
  row->key = control.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
  return row;
}

void MenuPresenter::RefreshMenu(std::uint32_t request_id,
                                const MenuTree& tree,
                                const ImageStore& images) {
  images_ = &images;
  const MenuImagesScope scope{images_};
  UpdateMenu(tree, 0, menu_.Items(), menu_rows_, request_id);
}

void MenuPresenter::UpdateMenu(
    const MenuTree& tree,
    std::size_t index,
    const foundation::Collections::IVector<MenuFlyoutItemBase>& output,
    std::vector<std::unique_ptr<NativeItem>>& rows,
    std::uint32_t request_id,
    std::size_t depth) {
  if (index >= tree.menus.size() || depth > tree.menus.size()) {
    output.Clear();
    rows.clear();
    return;
  }
  std::vector<std::unique_ptr<NativeItem>> next;
  next.reserve(tree.menus[index].size());
  for (const auto& item : tree.menus[index]) {
    if (!item.IsVisible())
      continue;
    const auto found = std::ranges::find_if(rows, [&](const auto& row) {
      return row && SameItem(row->item, item);
    });
    auto row =
        found != rows.end() ? std::move(*found) : CreateItem(item, request_id);
    row->item = item;
    next.push_back(std::move(row));
  }
  // Remove obsolete elements first. Otherwise deleting a preceding row would
  // unnecessarily detach every survivor to move it up one position.
  for (const auto& row : rows)
    if (row) {
      std::uint32_t previous = 0;
      if (output.IndexOf(row->element, previous))
        output.RemoveAt(previous);
    }
  rows = std::move(next);
  std::uint32_t position = 0;
  std::size_t radio_group = 0;
  for (const auto& row : rows) {
    const auto& item = row->item;
    // Insertions and removals touch only the changed row. Surviving elements
    // stay parented, retaining pointer presses, focus and open submenus.
    if (position == output.Size() || output.GetAt(position) != row->element) {
      std::uint32_t previous = 0;
      if (output.IndexOf(row->element, previous))
        output.RemoveAt(previous);
      output.InsertAt(position, row->element);
    }
    const auto kind = RowFor(item);
    if (kind == NativeRow::kSeparator) {
      ++radio_group;
    } else if (kind == NativeRow::kSubmenu) {
      const auto submenu = row->element.as<MenuFlyoutSubItem>();
      SetSubmenu(submenu, item);
      UpdateMenu(tree, static_cast<std::size_t>(item.submenu), submenu.Items(),
                 row->children, request_id, depth + 1);
    } else {
      const auto native = row->element.as<MenuFlyoutItem>();
      if (const auto radio = native.try_as<RadioMenuFlyoutItem>()) {
        const auto group = winrt::to_hstring(
            std::format("{}:{}:{}", request_id, index, radio_group));
        if (radio.GroupName() != group)
          radio.GroupName(group);
      }
      SetItem(native, item);
    }
    ++position;
  }
  while (output.Size() > position)
    output.RemoveAtEnd();
}

void MenuPresenter::SetSubmenu(const MenuFlyoutSubItem& submenu,
                               const MenuItem& item) {
  const auto label = winrt::to_hstring(item.label);
  if (submenu.Text() != label)
    submenu.Text(label);
  const auto access = winrt::to_hstring(item.access_key);
  if (submenu.AccessKey() != access)
    submenu.AccessKey(access);
  if (submenu.IsEnabled() != item.enabled)
    submenu.IsEnabled(item.enabled);
  UpdateIcon(submenu, item);
  const auto shortcut =
      winrt::to_hstring(MenuShortcutSymbols(item.accelerator));
  if (winrt::unbox_value_or<winrt::hstring>(submenu.Tag(), L"") != shortcut)
    submenu.Tag(winrt::box_value(shortcut));
  const auto accelerator = winrt::to_hstring(item.accelerator);
  if (xaml::Automation::AutomationProperties::GetAcceleratorKey(submenu) !=
      accelerator)
    xaml::Automation::AutomationProperties::SetAcceleratorKey(submenu,
                                                              accelerator);
}

void MenuPresenter::SetItem(const MenuFlyoutItem& native,
                            const MenuItem& item) {
  const auto label = winrt::to_hstring(item.label);
  if (native.Text() != label)
    native.Text(label);
  const auto access = winrt::to_hstring(item.access_key);
  if (native.AccessKey() != access)
    native.AccessKey(access);
  const bool enabled = item.enabled && item.type != MenuItemType::kTitle;
  if (native.IsEnabled() != enabled)
    native.IsEnabled(enabled);
  const auto shortcut =
      winrt::to_hstring(MenuShortcutSymbols(item.accelerator));
  if (native.KeyboardAcceleratorTextOverride() != shortcut)
    native.KeyboardAcceleratorTextOverride(shortcut);
  const auto accelerator = winrt::to_hstring(item.accelerator);
  if (xaml::Automation::AutomationProperties::GetAcceleratorKey(native) !=
      accelerator)
    xaml::Automation::AutomationProperties::SetAcceleratorKey(native,
                                                              accelerator);
  if (const auto toggle = native.try_as<ToggleMenuFlyoutItem>();
      toggle && toggle.IsChecked() != item.checked)
    toggle.IsChecked(item.checked);
  UpdateIcon(native, item);
}

void MenuPresenter::UpdateIcon(const MenuFlyoutItem& native,
                               const MenuItem& item) {
  SetIcon(native, item, item.glyph.empty() ? Bitmap(item.icon) : nullptr);
}

void MenuPresenter::UpdateIcon(const MenuFlyoutSubItem& native,
                               const MenuItem& item) {
  SetIcon(native, item, item.glyph.empty() ? Bitmap(item.icon) : nullptr);
}

void MenuPresenter::StyleItem(const controls::Control& item, bool submenu) {
  SetMenuItemResources(item);
  const bool checkable = !!item.try_as<ToggleMenuFlyoutItem>();
  const auto index = submenu ? 2u : (checkable ? 1u : 0u);
  if (item_template_failed_[index])
    return;
  try {
    auto& definition = item_templates_[index];
    if (!definition)
      definition = MenuItemTemplate(submenu, checkable);
    item.Template(definition);
    // Materialize here so a template failure can use the stock native menu
    // item instead of aborting the whole popup later during ShowAt.
    item.ApplyTemplate();
  } catch (const winrt::hresult_error& error) {
    item_template_failed_[index] = true;
    item.ClearValue(controls::Control::TemplateProperty());
    platform::WriteDefaultLog(
        LogLevel::kWarning,
        std::format("Menu item styling unavailable; using native template: {} "
                    "(0x{:08x})",
                    winrt::to_string(error.message()),
                    static_cast<std::uint32_t>(error.code().value)));
  }
}

}  // namespace kr::win
