#ifndef LUMA_STDLIB_GRAPHICALUI_CONSTANTS_HPP
#define LUMA_STDLIB_GRAPHICALUI_CONSTANTS_HPP

// Compile-time constants for GraphicalUi command types, subscription types,
// and widget types.  Centralises magic strings that were previously scattered
// across widget registration, command dispatch, and subscription management.

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Command type constants
// ═══════════════════════════════════════════════════════════

namespace cmd {

inline constexpr const char* none = "none";
inline constexpr const char* batch = "batch";
inline constexpr const char* delay = "delay";
inline constexpr const char* http_get = "http_get";
inline constexpr const char* http_post = "http_post";
inline constexpr const char* http_put = "http_put";
inline constexpr const char* http_delete = "http_delete";
inline constexpr const char* http_patch = "http_patch";
inline constexpr const char* random = "random";
inline constexpr const char* focus = "focus";
inline constexpr const char* announce = "announce";
inline constexpr const char* write_clipboard = "write_clipboard";
inline constexpr const char* read_clipboard = "read_clipboard";
inline constexpr const char* get_local_storage = "get_local_storage";
inline constexpr const char* set_local_storage = "set_local_storage";
inline constexpr const char* remove_local_storage = "remove_local_storage";
inline constexpr const char* clear_local_storage = "clear_local_storage";
inline constexpr const char* scroll_to = "scroll_to";
inline constexpr const char* blur = "blur";
inline constexpr const char* download_file = "download_file";
inline constexpr const char* notify = "notify";
inline constexpr const char* navigate = "navigate";
inline constexpr const char* navigate_back = "navigate_back";
inline constexpr const char* stylesheet = "stylesheet";
inline constexpr const char* load_stylesheet = "load_stylesheet";
inline constexpr const char* font_face = "font_face";
inline constexpr const char* set_theme_mode = "set_theme_mode";
inline constexpr const char* open_url = "open_url";
inline constexpr const char* set_title = "set_title";
inline constexpr const char* print = "print";
inline constexpr const char* debounce = "debounce";

} // namespace cmd

// ═══════════════════════════════════════════════════════════
// Navigation message constants (values for the _gui_nav key)
// ═══════════════════════════════════════════════════════════

namespace nav {

inline constexpr const char* navigate = "navigate";
inline constexpr const char* navigate_back = "navigate_back";

// Reserved message prefix delivered by navigation_link("...", "navigate:<route>").
// apply_event_result interprets it so the click drives the same routing flow as
// the GraphicalUi.navigate command.
inline constexpr const char* navigate_prefix = "navigate:";

} // namespace nav

// ═══════════════════════════════════════════════════════════
// Subscription type constants
// ═══════════════════════════════════════════════════════════

namespace sub {

inline constexpr const char* timer = "timer";
inline constexpr const char* keyboard = "keyboard";
inline constexpr const char* resize = "resize";
inline constexpr const char* focus = "focus";
inline constexpr const char* mouse = "mouse";
inline constexpr const char* visibility = "visibility";
inline constexpr const char* online = "online";
inline constexpr const char* offline = "offline";
inline constexpr const char* media_query = "media_query";
inline constexpr const char* scroll = "scroll";
inline constexpr const char* idle = "idle";
inline constexpr const char* storage = "storage";
inline constexpr const char* animation_frame = "animation_frame";
inline constexpr const char* drag = "drag";

} // namespace sub

// ═══════════════════════════════════════════════════════════
// Widget type constants
// ═══════════════════════════════════════════════════════════

namespace wtype {

inline constexpr const char* label = "label";
inline constexpr const char* heading = "heading";
inline constexpr const char* button = "button";
inline constexpr const char* text_input = "text_input";
inline constexpr const char* text_area = "text_area";
inline constexpr const char* checkbox = "checkbox";
inline constexpr const char* dropdown = "dropdown";
inline constexpr const char* slider = "slider";
inline constexpr const char* progress = "progress";
inline constexpr const char* spinner = "spinner";
inline constexpr const char* image = "image";
inline constexpr const char* separator = "separator";
inline constexpr const char* spacer = "spacer";
inline constexpr const char* file_input = "file_input";
inline constexpr const char* date_picker = "date_picker";
inline constexpr const char* time_picker = "time_picker";
inline constexpr const char* color_picker = "color_picker";
inline constexpr const char* row = "row";
inline constexpr const char* column = "column";
inline constexpr const char* panel = "panel";
inline constexpr const char* list = "list";
inline constexpr const char* radio_group = "radio_group";
inline constexpr const char* toggle = "toggle";
inline constexpr const char* switch_ = "switch";
inline constexpr const char* tabs = "tabs";
inline constexpr const char* table = "table";
inline constexpr const char* dialog = "dialog";
inline constexpr const char* alert = "alert";
inline constexpr const char* tooltip = "tooltip";
inline constexpr const char* link = "link";
inline constexpr const char* icon = "icon";
inline constexpr const char* toolbar = "toolbar";
inline constexpr const char* grid = "grid";
inline constexpr const char* wrapped_row = "wrapped_row";
inline constexpr const char* scroll_row = "scroll_row";
inline constexpr const char* scroll_column = "scroll_column";
inline constexpr const char* nearby = "nearby";
inline constexpr const char* debug = "debug";
inline constexpr const char* transition = "transition";
inline constexpr const char* animate = "animate";
inline constexpr const char* badge = "badge";
inline constexpr const char* accordion = "accordion";
inline constexpr const char* breadcrumb = "breadcrumb";
inline constexpr const char* avatar = "avatar";
inline constexpr const char* card = "card";
inline constexpr const char* skeleton = "skeleton";
inline constexpr const char* number_input = "number_input";
inline constexpr const char* search_input = "search_input";
inline constexpr const char* toast = "toast";
inline constexpr const char* toast_region = "toast_region";
inline constexpr const char* empty_state = "empty_state";
inline constexpr const char* wizard = "wizard";
inline constexpr const char* form = "form";
inline constexpr const char* field_error = "field_error";
inline constexpr const char* draggable = "draggable";
inline constexpr const char* drop_target = "drop_target";
inline constexpr const char* paginator = "paginator";
inline constexpr const char* infinite_scroll = "infinite_scroll";
inline constexpr const char* inspect = "inspect";
inline constexpr const char* accessible = "accessible";
inline constexpr const char* keyed = "keyed";
inline constexpr const char* aria_live = "aria_live";
inline constexpr const char* aria_describedby = "aria_describedby";
inline constexpr const char* virtual_list = "virtual_list";
inline constexpr const char* error_boundary = "error_boundary";
inline constexpr const char* navigation_link = "navigation_link";
inline constexpr const char* vertical_bar_chart = "vertical_bar_chart";
inline constexpr const char* line_chart = "line_chart";
inline constexpr const char* pie_chart = "pie_chart";
inline constexpr const char* area_chart = "area_chart";
inline constexpr const char* horizontal_bar_chart = "horizontal_bar_chart";
inline constexpr const char* donut_chart = "donut_chart";
inline constexpr const char* scatter_plot = "scatter_plot";
inline constexpr const char* menu = "menu";
inline constexpr const char* popover = "popover";
inline constexpr const char* combobox = "combobox";
inline constexpr const char* field = "field";
inline constexpr const char* confirm = "confirm";

} // namespace wtype

// ═══════════════════════════════════════════════════════════
// Nearby widget position constants
// ═══════════════════════════════════════════════════════════

namespace pos {

inline constexpr const char* above = "above";
inline constexpr const char* below = "below";
inline constexpr const char* left = "left";
inline constexpr const char* right = "right";
inline constexpr const char* in_front = "in-front";
inline constexpr const char* behind = "behind";

} // namespace pos

// ═══════════════════════════════════════════════════════════
// Internal dictionary key constants
// ═══════════════════════════════════════════════════════════
//
// These are the underscore-prefixed keys used internally by
// widget dictionaries to carry metadata (command type,
// callbacks, model bindings, etc.).

namespace key {

inline constexpr const char* command_type = "_command_type";
inline constexpr const char* sub_type = "_sub_type";
inline constexpr const char* callback = "_callback";
inline constexpr const char* callback_id = "_callback_id";
inline constexpr const char* deferred_callback = "_deferred_callback";
inline constexpr const char* deferred_close_callback = "_deferred_close_callback";
inline constexpr const char* deferred_clear_callback = "_deferred_clear_callback";
inline constexpr const char* deferred_select_callback = "_deferred_select_callback";
inline constexpr const char* deferred_action_callback = "_deferred_action_callback";
inline constexpr const char* deferred_sort_callback = "_deferred_sort_callback";
inline constexpr const char* deferred_commit_callback = "_deferred_commit_callback";
inline constexpr const char* deferred_command_callback = "_deferred_command_callback";
inline constexpr const char* element_id = "_element_id";
inline constexpr const char* role = "_role";
inline constexpr const char* gui_model = "_gui_model";
inline constexpr const char* gui_command = "_gui_command";
inline constexpr const char* gui_nav = "_gui_nav";
inline constexpr const char* route_history = "_route_history";

} // namespace key

} // namespace luma::gui_detail

#endif // LUMA_STDLIB_GRAPHICALUI_CONSTANTS_HPP
