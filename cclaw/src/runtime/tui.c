// tui.c - Terminal UI implementation for CClaw
// SPDX-License-Identifier: MIT

#include "runtime/tui.h"
#include "core/alloc.h"
#include "core/agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>

// Global TUI instance for signal handling
static tui_t* g_tui = NULL;

typedef struct tui_border_chars_t {
    const char* ul;
    const char* ur;
    const char* ll;
    const char* lr;
    const char* h;
    const char* v;
} tui_border_chars_t;

static tui_border_chars_t tui_get_border_chars(const tui_t* tui) {
    const bool unicode = tui && tui->config.theme.use_unicode && tui_supports_unicode();
    if (unicode) {
        return (tui_border_chars_t){
            .ul = "╔", .ur = "╗", .ll = "╚", .lr = "╝", .h = "═", .v = "║"
        };
    }
    return (tui_border_chars_t){
        .ul = "+", .ur = "+", .ll = "+", .lr = "+", .h = "-", .v = "|"
    };
}

static tui_theme_t tui_theme_high_contrast(void) {
    return (tui_theme_t){
        .color_bg = 0,
        .color_fg = 15,
        .color_primary = 51,
        .color_secondary = 226,
        .color_success = 46,
        .color_warning = 220,
        .color_error = 196,
        .color_muted = 250,
        .use_bold = true,
        .use_italic = false,
        .use_unicode = true
    };
}

static tui_theme_t tui_theme_minimal(void) {
    return (tui_theme_t){
        .color_bg = 16,
        .color_fg = 250,
        .color_primary = 250,
        .color_secondary = 244,
        .color_success = 248,
        .color_warning = 248,
        .color_error = 248,
        .color_muted = 240,
        .use_bold = false,
        .use_italic = false,
        .use_unicode = false
    };
}

static const char* tui_session_status_icon(const tui_t* tui, const agent_session_t* session, bool is_active) {
    const bool unicode = tui_supports_unicode();
    if (!tui || !session) return unicode ? "·" : ".";
    if (is_active && tui->zc_turn_cancelling) return unicode ? "⏸" : "!";
    if (is_active && tui->zc_turn_inflight) return unicode ? "▶" : ">";
    if (session->total_messages == 0) return unicode ? "○" : "o";
    if (is_active) return unicode ? "●" : "*";
    return unicode ? "·" : ".";
}

static void tui_session_preview(const agent_session_t* session, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!session) return;

    if (session->current && session->current->content.data && session->current->content.len > 0) {
        snprintf(out, out_size, "%s", session->current->content.data);
        return;
    }

    if (session->model.data && session->model.len > 0) {
        snprintf(out, out_size, "model: %s", session->model.data);
        return;
    }

    snprintf(out, out_size, "new session");
}

static int32_t tui_get_active_session_index(const tui_t* tui) {
    if (!tui || !tui->agent || !tui->agent->ctx || !tui->agent->ctx->active_session) {
        return -1;
    }
    for (uint32_t i = 0; i < tui->agent->ctx->session_count; i++) {
        if (tui->agent->ctx->sessions[i] == tui->agent->ctx->active_session) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t tui_get_view_session_index(const tui_t* tui) {
    if (!tui || !tui->agent || !tui->agent->ctx || tui->agent->ctx->session_count == 0) {
        return -1;
    }

    if (tui->active_panel == TUI_PANEL_SIDEBAR) {
        if (tui->selected_session < tui->agent->ctx->session_count) {
            return (int32_t)tui->selected_session;
        }
    }

    return tui_get_active_session_index(tui);
}

static bool tui_message_visible_for_session(const tui_message_t* msg, int32_t view_session_index) {
    if (!msg) return false;
    if (view_session_index < 0) return true;
    return msg->session_index < 0 || msg->session_index == view_session_index;
}

static bool tui_is_tool_sender(const char* sender) {
    if (!sender) return false;
    return strcmp(sender, "tool_call") == 0 || strcmp(sender, "tool_result") == 0;
}

static void tui_compact_tool_text(const char* text, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!text) return;

    size_t n = strcspn(text, "\r\n");
    if (n > 96) n = 96;
    if (n + 4 >= out_size) n = out_size > 4 ? out_size - 4 : 0;
    if (n > 0) {
        memcpy(out, text, n);
        out[n] = '\0';
    }
    if (text[n] != '\0') {
        strncat(out, "...", out_size - strlen(out) - 1);
    }
}

static void tui_session_append_tree_message(
    tui_t* tui,
    agent_message_type_t type,
    const char* text,
    const char* tool_name,
    const char* tool_args,
    const char* tool_result
) {
    if (!tui || tui->suppress_session_persist || !tui->agent || !tui->agent->ctx) return;

    agent_session_t* session = tui->agent->ctx->active_session;
    if (!session) return;

    str_t content = str_dup_cstr(text ? text : "", NULL);
    agent_message_t* msg = agent_message_create(type, &content);
    free((void*)content.data);
    if (!msg) return;

    if (tool_name) msg->tool_name = str_dup_cstr(tool_name, NULL);
    if (tool_args) msg->tool_args = str_dup_cstr(tool_args, NULL);
    if (tool_result) msg->tool_result = str_dup_cstr(tool_result, NULL);

    if (session->current) {
        agent_message_add_child(session->current, msg);
    } else {
        session->root = msg;
    }
    session->current = msg;
    session->total_messages++;
    session->last_active = msg->timestamp;
}

static void tui_format_tool_text(
    char* out,
    size_t out_size,
    const char* phase,
    const char* tool_name,
    const char* payload
) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    const char* name = (tool_name && tool_name[0] != '\0') ? tool_name : "tool";
    const char* data = (payload && payload[0] != '\0') ? payload : "";
    if (strcmp(phase, "start") == 0) {
        snprintf(out, out_size, "[tool:start] %s %s", name, data);
    } else {
        snprintf(out, out_size, "[tool:end] %s %s", name, data);
    }
}

static void tui_chat_add_message_internal_ex(
    tui_t* tui,
    const char* sender,
    const char* text,
    int32_t forced_session_index,
    bool persist
) {
    if (!tui || !text) return;

    tui_message_t* msg = calloc(1, sizeof(tui_message_t));
    if (!msg) return;

    msg->sender = strdup(sender);
    msg->text = strdup(text);
    msg->timestamp = 0; // TODO: get actual timestamp
    msg->session_index = forced_session_index >= -1 ? forced_session_index : tui_get_active_session_index(tui);
    msg->next = NULL;

    // Add to linked list
    if (tui->messages_tail) {
        tui->messages_tail->next = msg;
    } else {
        tui->messages = msg;
    }
    tui->messages_tail = msg;
    tui->message_count++;

    // Limit message count to prevent memory issues
    if (tui->message_count > 1000) {
        tui_message_t* old = tui->messages;
        tui->messages = old->next;
        free(old->text);
        free(old->sender);
        free(old);
        tui->message_count--;
    }

    if (persist && tui->use_zeroclaw_session) {
        if (strcmp(sender, "user") == 0) {
            tui_session_append_tree_message(tui, AGENT_MSG_USER, text, NULL, NULL, NULL);
        } else if (strcmp(sender, "assistant") == 0) {
            tui_session_append_tree_message(tui, AGENT_MSG_ASSISTANT, text, NULL, NULL, NULL);
        } else {
            tui_session_append_tree_message(tui, AGENT_MSG_SYSTEM, text, NULL, NULL, NULL);
        }
    }
}

static bool tui_has_messages_for_session(const tui_t* tui, int32_t session_index) {
    if (!tui || session_index < 0) return false;
    for (tui_message_t* m = tui->messages; m; m = m->next) {
        if (m->session_index == session_index) return true;
    }
    return false;
}

static void tui_hydrate_session_messages(tui_t* tui, int32_t session_index) {
    if (!tui || !tui->agent || !tui->agent->ctx || session_index < 0) return;
    if ((uint32_t)session_index >= tui->agent->ctx->session_count) return;
    if (tui_has_messages_for_session(tui, session_index)) return;

    agent_session_t* session = tui->agent->ctx->sessions[session_index];
    if (!session || !session->current) return;

    // Build root->current path via parent pointers from current.
    uint32_t depth = 0;
    for (agent_message_t* p = session->current; p; p = p->parent) {
        depth++;
    }
    if (depth == 0) return;

    agent_message_t** path = calloc(depth, sizeof(agent_message_t*));
    if (!path) return;
    uint32_t i = depth;
    for (agent_message_t* p = session->current; p; p = p->parent) {
        if (i == 0) break;
        path[--i] = p;
    }

    tui->suppress_session_persist = true;
    for (uint32_t k = 0; k < depth; k++) {
        agent_message_t* m = path[k];
        if (!m) continue;

        if (m->type == AGENT_MSG_USER) {
            tui_chat_add_message_internal_ex(tui, "user", m->content.data ? m->content.data : "", session_index, false);
        } else if (m->type == AGENT_MSG_ASSISTANT || m->type == AGENT_MSG_SUMMARY) {
            tui_chat_add_message_internal_ex(tui, "assistant", m->content.data ? m->content.data : "", session_index, false);
        } else if (m->type == AGENT_MSG_TOOL_CALL) {
            char line[512];
            tui_format_tool_text(
                line,
                sizeof(line),
                "start",
                m->tool_name.data,
                m->tool_args.data ? m->tool_args.data : (m->content.data ? m->content.data : "")
            );
            tui_chat_add_message_internal_ex(tui, "tool_call", line, session_index, false);
        } else if (m->type == AGENT_MSG_TOOL_RESULT) {
            char line[512];
            tui_format_tool_text(
                line,
                sizeof(line),
                "end",
                m->tool_name.data,
                m->tool_result.data ? m->tool_result.data : (m->content.data ? m->content.data : "")
            );
            tui_chat_add_message_internal_ex(tui, "tool_result", line, session_index, false);
        } else {
            tui_chat_add_message_internal_ex(tui, "system", m->content.data ? m->content.data : "", session_index, false);
        }
    }
    tui->suppress_session_persist = false;
    free(path);
}

static void tui_apply_theme_mode(tui_t* tui, uint8_t mode) {
    if (!tui) return;
    switch (mode % 5) {
        case 0: tui->config.theme = tui_theme_default(); break;
        case 1: tui->config.theme = tui_theme_dark(); break;
        case 2: tui->config.theme = tui_theme_light(); break;
        case 3: tui->config.theme = tui_theme_high_contrast(); break;
        default: tui->config.theme = tui_theme_minimal(); break;
    }
    tui->theme_mode = (uint8_t)(mode % 5);
}

static const char* tui_theme_mode_name(uint8_t mode) {
    switch (mode % 5) {
        case 0: return "default";
        case 1: return "dark";
        case 2: return "light";
        case 3: return "high-contrast";
        default: return "minimal";
    }
}

// ============================================================================
// Terminal Control
// ============================================================================

void tui_get_terminal_size(uint16_t* out_width, uint16_t* out_height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *out_width = ws.ws_col;
        *out_height = ws.ws_row;
    } else {
        *out_width = TUI_DEFAULT_WIDTH;
        *out_height = TUI_DEFAULT_HEIGHT;
    }
}

bool tui_supports_color(void) {
    const char* term = getenv("TERM");
    if (!term) return false;
    return strstr(term, "color") != NULL ||
           strcmp(term, "xterm") == 0 ||
           strcmp(term, "screen") == 0 ||
           strcmp(term, "tmux") == 0;
}

bool tui_supports_unicode(void) {
    const char* lang = getenv("LANG");
    if (lang && strstr(lang, "UTF-8")) return true;
    return false;
}

// ============================================================================
// Theme
// ============================================================================

tui_theme_t tui_theme_default(void) {
    return (tui_theme_t){
        .color_bg = 16,
        .color_fg = 252,
        .color_primary = 33,
        .color_secondary = 45,
        .color_success = 42,
        .color_warning = 220,
        .color_error = 196,
        .color_muted = 244,
        .use_bold = true,
        .use_italic = false,
        .use_unicode = true
    };
}

tui_theme_t tui_theme_dark(void) {
    return (tui_theme_t){
        .color_bg = 16,
        .color_fg = 252,
        .color_primary = 75,
        .color_secondary = 39,
        .color_success = 42,
        .color_warning = 214,
        .color_error = 203,
        .color_muted = 243,
        .use_bold = true,
        .use_italic = false,
        .use_unicode = true
    };
}

tui_theme_t tui_theme_light(void) {
    return (tui_theme_t){
        .color_bg = 15,
        .color_fg = 0,
        .color_primary = 4,
        .color_secondary = 6,
        .color_success = 2,
        .color_warning = 3,
        .color_error = 1,
        .color_muted = 8,
        .use_bold = true,
        .use_italic = false,
        .use_unicode = true
    };
}

void tui_theme_apply(tui_t* tui, const tui_theme_t* theme) {
    if (tui && theme) {
        tui->config.theme = *theme;
    }
}

// ============================================================================
// ANSI Drawing
// ============================================================================

void tui_move_cursor(uint16_t x, uint16_t y) {
    printf("\033[%d;%dH", y + 1, x + 1);
}

void tui_set_color(uint8_t fg, uint8_t bg) {
    printf("\033[38;5;%dm\033[48;5;%dm", fg, bg);
}

void tui_reset_color(void) {
    printf("\033[0m");
}

void tui_clear_screen(tui_t* tui) {
    (void)tui;
    printf(TUI_CLEAR_SCREEN TUI_CURSOR_HOME);
    fflush(stdout);
}

void tui_draw_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* title) {
    if (w < 2 || h < 2) {
        return;
    }
    const tui_border_chars_t b = tui_get_border_chars(g_tui);

    // Top border
    tui_move_cursor(x, y);
    printf("%s", b.ul);
    for (uint16_t i = 0; i < w - 2; i++) printf("%s", b.h);
    printf("%s", b.ur);

    // Title
    if (title && strlen(title) > 0) {
        tui_move_cursor(x + 2, y);
        printf(" %s ", title);
    }

    // Side borders
    for (uint16_t i = 1; i < h - 1; i++) {
        tui_move_cursor(x, y + i);
        printf("%s", b.v);
        tui_move_cursor(x + w - 1, y + i);
        printf("%s", b.v);
    }

    // Bottom border
    tui_move_cursor(x, y + h - 1);
    printf("%s", b.ll);
    for (uint16_t i = 0; i < w - 2; i++) printf("%s", b.h);
    printf("%s", b.lr);
}

void tui_draw_line(uint16_t x, uint16_t y, uint16_t len, bool horizontal) {
    if (len == 0) return;
    tui_move_cursor(x, y);
    const tui_border_chars_t b = tui_get_border_chars(g_tui);

    if (horizontal) {
        for (uint16_t i = 0; i < len; i++) {
            printf("%s", b.h);
        }
    } else {
        for (uint16_t i = 0; i < len; i++) {
            tui_move_cursor(x, y + i);
            printf("%s", b.v);
        }
    }
}

void tui_draw_text(uint16_t x, uint16_t y, const char* text) {
    tui_move_cursor(x, y);
    printf("%s", text);
}

void tui_draw_text_truncated(uint16_t x, uint16_t y, uint16_t max_width, const char* text) {
    if (!text || max_width == 0) return;
    tui_move_cursor(x, y);
    size_t len = strlen(text);
    if (len > max_width) {
        if (max_width <= 3) {
            printf("%.*s", (int)max_width, text);
        } else {
            printf("%.*s...", (int)max_width - 3, text);
        }
    } else {
        printf("%s", text);
    }
}

// ============================================================================
// TUI Lifecycle
// ============================================================================

tui_config_t tui_config_default(void) {
    uint16_t w, h;
    tui_get_terminal_size(&w, &h);

    return (tui_config_t){
        .width = w,
        .height = h,
        .use_color = tui_supports_color(),
        .use_mouse = false,
        .show_token_count = true,
        .show_timestamps = false,
        .show_branch_indicator = true,
        .theme = tui_theme_default()
    };
}

err_t tui_create(const tui_config_t* config, tui_t** out_tui) {
    if (!out_tui) return ERR_INVALID_ARGUMENT;

    tui_t* tui = calloc(1, sizeof(tui_t));
    if (!tui) return ERR_OUT_OF_MEMORY;

    tui->config = config ? *config : tui_config_default();
    tui->running = false;
    tui->needs_redraw = true;
    tui->theme_mode = 0;
    tui->suppress_session_persist = false;
    tui->show_tool_details = false;
    tui_apply_theme_mode(tui, tui->theme_mode);

    // Allocate input buffer
    tui->input_capacity = TUI_MAX_INPUT_LENGTH;
    tui->input_buffer = malloc(tui->input_capacity);
    if (!tui->input_buffer) {
        free(tui);
        return ERR_OUT_OF_MEMORY;
    }
    tui->input_buffer[0] = '\0';

    // Allocate history
    tui->history_capacity = TUI_INPUT_HISTORY_SIZE;
    tui->history = calloc(tui->history_capacity, sizeof(char*));
    if (!tui->history) {
        free(tui->input_buffer);
        free(tui);
        return ERR_OUT_OF_MEMORY;
    }

    // Create panels
    for (int i = 0; i < 5; i++) {
        tui->panels[i] = calloc(1, sizeof(tui_panel_t));
        if (!tui->panels[i]) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                free(tui->panels[j]);
            }
            free(tui->history);
            free(tui->input_buffer);
            free(tui);
            return ERR_OUT_OF_MEMORY;
        }
        tui->panels[i]->type = i;
        tui->panels[i]->visible = true;
    }

    // Initialize message list
    tui->messages = NULL;
    tui->messages_tail = NULL;
    tui->message_count = 0;
    tui->selected_session = 0;

    g_tui = tui;
    *out_tui = tui;
    return ERR_OK;
}

void tui_destroy(tui_t* tui) {
    if (!tui) return;

    tui_restore_terminal(tui);

    free(tui->input_buffer);

    for (uint32_t i = 0; i < tui->history_count; i++) {
        free(tui->history[i]);
    }
    free(tui->history);

    // Free messages
    tui_message_t* msg = tui->messages;
    while (msg) {
        tui_message_t* next = msg->next;
        free(msg->text);
        free(msg->sender);
        free(msg);
        msg = next;
    }

    for (int i = 0; i < 5; i++) {
        free(tui->panels[i]);
    }

    free(tui);

    if (g_tui == tui) {
        g_tui = NULL;
    }
}

err_t tui_init_terminal(tui_t* tui) {
    if (!tui) return ERR_INVALID_ARGUMENT;

    // Check if stdin is a TTY
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Error: TUI requires an interactive terminal (TTY)\n");
        return ERR_FAILED;
    }

    // Save original terminal settings
    if (tcgetattr(STDIN_FILENO, &tui->original_termios) != 0) {
        perror("tcgetattr failed");
        return ERR_FAILED;
    }

    // Set raw mode
    struct termios raw = tui->original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // 100ms timeout

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return ERR_FAILED;
    }

    tui->raw_mode = true;

    // Hide cursor
    printf(TUI_CURSOR_HIDE);
    fflush(stdout);

    return ERR_OK;
}

void tui_restore_terminal(tui_t* tui) {
    if (!tui || !tui->raw_mode) return;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tui->original_termios);
    tui->raw_mode = false;

    // Show cursor
    printf(TUI_CURSOR_SHOW TUI_COLOR_RESET "\n");
    fflush(stdout);
}

static void resize_handler(int sig) {
    (void)sig;
    if (g_tui) {
        tui_get_terminal_size(&g_tui->config.width, &g_tui->config.height);
        g_tui->needs_redraw = true;
    }
}

static void tui_format_summary(char* out, size_t out_size, const char* text, size_t max_chars) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!text) return;

    // Keep the first line only for compact status rendering.
    size_t n = strcspn(text, "\r\n");
    if (n > max_chars) {
        n = max_chars;
    }

    if (n + 4 >= out_size) {
        n = out_size > 4 ? out_size - 4 : 0;
    }

    if (n > 0) {
        memcpy(out, text, n);
        out[n] = '\0';
    }

    if (text[n] != '\0') {
        strncat(out, "...", out_size - strlen(out) - 1);
    }
}

static void tui_poll_zeroclaw_events(tui_t* tui, uint32_t max_events) {
    if (!tui || !tui->use_zeroclaw_session || !tui->zc_session) {
        return;
    }

    for (uint32_t i = 0; i < max_events; i++) {
        zc_event_t ev = {0};
        zc_result_t poll_res = zc_session_poll_event(tui->zc_session, &ev, 0);
        if (poll_res != ZC_OK || ev.type == ZC_EVT_NONE) {
            break;
        }

        if (ev.type == ZC_EVT_ASSISTANT_TEXT) {
            if (ev.payload) {
                tui_chat_add_assistant_message(tui, ev.payload);
            }
        } else if (ev.type == ZC_EVT_TOOL_START) {
            if (ev.name) {
                char args_summary[140];
                tui_format_summary(args_summary, sizeof(args_summary), ev.payload, 96);
                tui_chat_add_tool_call(tui, ev.name, args_summary);
            }
        } else if (ev.type == ZC_EVT_TOOL_END) {
            if (ev.name) {
                char result_summary[140];
                tui_format_summary(result_summary, sizeof(result_summary), ev.payload, 96);
                tui_chat_add_tool_result(tui, ev.name, result_summary);
            }
        } else if (ev.type == ZC_EVT_ERROR) {
            if (ev.payload) {
                tui_chat_add_system_message(tui, ev.payload);
            } else {
                tui_chat_add_system_message(tui, "Error: ZeroClaw session failed");
            }
            tui->zc_turn_inflight = false;
            tui->zc_turn_cancelling = false;
            tui->zc_active_turn_id = 0;
        } else if (ev.type == ZC_EVT_CANCELLED) {
            tui_chat_add_system_message(tui, "Cancelled");
            tui->zc_turn_inflight = false;
            tui->zc_turn_cancelling = false;
            tui->zc_active_turn_id = 0;
        } else if (ev.type == ZC_EVT_TURN_DONE) {
            tui->zc_turn_inflight = false;
            tui->zc_turn_cancelling = false;
            tui->zc_active_turn_id = 0;
        }

        zc_session_free_event(&ev);
        tui->needs_redraw = true;
    }
}

err_t tui_run(tui_t* tui, agent_t* agent) {
    if (!tui || !agent) return ERR_INVALID_ARGUMENT;

    tui->agent = agent;
    tui->running = true;

    // Initialize terminal
    err_t err = tui_init_terminal(tui);
    if (err != ERR_OK) {
        return err;
    }

    // Setup resize handler
    signal(SIGWINCH, resize_handler);

    // Initial draw
    tui_redraw(tui);

    // Main loop
    while (tui->running) {
        tui_poll_zeroclaw_events(tui, 32);

        if (tui->needs_redraw) {
            tui_redraw(tui);
            tui->needs_redraw = false;
        }

        tui_process_input(tui);
    }

    return ERR_OK;
}

void tui_stop(tui_t* tui) {
    if (tui) {
        tui->running = false;
    }
}

// ============================================================================
// Rendering
// ============================================================================

void tui_refresh(tui_t* tui) {
    fflush(stdout);
}

void tui_redraw(tui_t* tui) {
    if (!tui) return;

    tui_clear_screen(tui);

    // Draw panels
    tui_draw_toolbar(tui);
    tui_draw_sidebar(tui);
    tui_draw_chat_panel(tui);
    tui_draw_status_bar(tui);
    tui_draw_input_area(tui);

    tui_refresh(tui);
}

void tui_draw_toolbar(tui_t* tui) {
    if (!tui || tui->config.width < 20 || tui->config.height < 8) return;

    const uint16_t w = tui->config.width;
    const tui_border_chars_t b = tui_get_border_chars(tui);
    tui_set_color(tui->config.theme.color_secondary, tui->config.theme.color_bg);
    tui_draw_box(0, 0, w, 3, NULL);

    tui_set_color(15, tui->config.theme.color_primary);
    tui_move_cursor(2, 1);
    printf("CClaw Agent TUI");

    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);
    tui_move_cursor(20, 1);
    printf("Ctrl+H Help");
    tui_move_cursor(33, 1);
    printf("%s Ctrl+N New", b.v);
    tui_move_cursor(47, 1);
    printf("%s Ctrl+B Branch", b.v);
    tui_move_cursor(64, 1);
    printf("%s Ctrl+C Cancel", b.v);
    tui_move_cursor(81, 1);
    printf("%s Ctrl+Q Quit", b.v);
    if (tui->config.width > 108) {
        tui_move_cursor(95, 1);
        printf("%s Ctrl+T Theme", b.v);
    }
    if (tui->config.width > 124) {
        tui_move_cursor(111, 1);
        printf("%s Ctrl+O Tools", b.v);
    }
    tui_reset_color();
}

static const char* tui_sender_prefix(const tui_message_t* msg) {
    const bool unicode = tui_supports_unicode();
    if (!msg || !msg->sender) return unicode ? "∙ Msg  " : "* Msg  ";

    if (strcmp(msg->sender, "user") == 0) {
        return unicode ? "◉ You  " : "> You  ";
    }
    if (strcmp(msg->sender, "assistant") == 0) {
        return unicode ? "◆ AI   " : "# AI   ";
    }
    if (strcmp(msg->sender, "tool_call") == 0) {
        return unicode ? "⚙ Call " : "@ Call ";
    }
    if (strcmp(msg->sender, "tool_result") == 0) {
        return unicode ? "✓ Done " : "+ Done ";
    }
    if (strcmp(msg->sender, "system") == 0) {
        if (msg->text && strncmp(msg->text, "[tool:start]", 12) == 0) {
            return unicode ? "⚙ Tool " : "@ Tool ";
        }
        if (msg->text && strncmp(msg->text, "[tool:end]", 10) == 0) {
            return unicode ? "✓ Tool " : "+ Tool ";
        }
        return unicode ? "· Sys  " : "- Sys  ";
    }
    return unicode ? "∙ Msg  " : "* Msg  ";
}

static uint8_t tui_sender_color(const tui_t* tui, const tui_message_t* msg) {
    if (!tui || !msg || !msg->sender) return 7;
    if (strcmp(msg->sender, "user") == 0) return tui->config.theme.color_success;
    if (strcmp(msg->sender, "assistant") == 0) return tui->config.theme.color_primary;
    if (strcmp(msg->sender, "tool_call") == 0) return tui->config.theme.color_warning;
    if (strcmp(msg->sender, "tool_result") == 0) return tui->config.theme.color_secondary;
    if (msg->text) {
        if (strncmp(msg->text, "[tool:start]", 12) == 0) return tui->config.theme.color_warning;
        if (strncmp(msg->text, "[tool:end]", 10) == 0) return tui->config.theme.color_secondary;
        if (strstr(msg->text, "Error") == msg->text || strstr(msg->text, "error") == msg->text) {
            return tui->config.theme.color_error;
        }
    }
    return tui->config.theme.color_muted;
}

static uint32_t tui_wrapped_lines_for_message(const tui_message_t* msg, uint16_t max_width) {
    if (!msg || !msg->text || max_width == 0) return 0;

    const char* prefix = tui_sender_prefix(msg);
    const size_t prefix_len = strlen(prefix);
    const uint16_t first_room = (max_width > prefix_len) ? (uint16_t)(max_width - prefix_len) : 1;
    const uint16_t next_room = (max_width > prefix_len) ? (uint16_t)(max_width - prefix_len) : 1;

    uint32_t lines = 0;
    const char* cursor = msg->text;
    char compact_tool[160];
    if (g_tui && !g_tui->show_tool_details && tui_is_tool_sender(msg->sender)) {
        tui_compact_tool_text(msg->text, compact_tool, sizeof(compact_tool));
        cursor = compact_tool;
    }
    bool first_line = true;
    while (true) {
        const char* br = strchr(cursor, '\n');
        const size_t seg_len = br ? (size_t)(br - cursor) : strlen(cursor);
        size_t consumed = 0;
        do {
            uint16_t room = first_line ? first_room : next_room;
            size_t take = seg_len - consumed;
            if (take > room) take = room;
            (void)take;
            lines++;
            consumed += take;
            first_line = false;
        } while (consumed < seg_len);

        if (!br) break;
        cursor = br + 1;
        if (*cursor == '\0') {
            lines++;
            break;
        }
    }

    return lines;
}

static void tui_render_wrapped_message(
    tui_t* tui,
    const tui_message_t* msg,
    uint16_t x,
    uint16_t* inout_y,
    uint16_t max_width,
    uint16_t max_lines,
    uint32_t* inout_skip
) {
    if (!tui || !msg || !msg->text || !inout_y || !inout_skip || max_width == 0 || max_lines == 0) {
        return;
    }

    const char* prefix = tui_sender_prefix(msg);
    const char* continuation = tui_supports_unicode() ? "│      " : "|      ";
    const size_t prefix_len = strlen(prefix);
    const uint16_t first_room = (max_width > prefix_len) ? (uint16_t)(max_width - prefix_len) : 1;
    const uint16_t next_room = (max_width > prefix_len) ? (uint16_t)(max_width - prefix_len) : 1;
    const uint8_t sender_color = tui_sender_color(tui, msg);

    char indent[32];
    size_t indent_len = strlen(continuation);
    if (indent_len >= sizeof(indent)) indent_len = sizeof(indent) - 1;
    memcpy(indent, continuation, indent_len);
    indent[indent_len] = '\0';

    const char* cursor = msg->text;
    char compact_tool[160];
    if (!tui->show_tool_details && tui_is_tool_sender(msg->sender)) {
        tui_compact_tool_text(msg->text, compact_tool, sizeof(compact_tool));
        cursor = compact_tool;
    }
    bool first_line = true;
    while (*inout_y < max_lines) {
        const char* br = strchr(cursor, '\n');
        const size_t seg_len = br ? (size_t)(br - cursor) : strlen(cursor);
        size_t consumed = 0;

        do {
            uint16_t room = first_line ? first_room : next_room;
            size_t take = seg_len - consumed;
            if (take > room) take = room;

            if (*inout_skip > 0) {
                (*inout_skip)--;
            } else if (*inout_y < max_lines) {
                tui_move_cursor(x, *inout_y);
                tui_set_color(sender_color, tui->config.theme.color_bg);
                printf("%s", first_line ? prefix : indent);
                tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);
                printf("%.*s", (int)take, cursor + consumed);
                (*inout_y)++;
            }

            consumed += take;
            first_line = false;
        } while (consumed < seg_len && *inout_y < max_lines);

        if (!br || *inout_y >= max_lines) break;
        cursor = br + 1;
        if (*cursor == '\0') {
            if (*inout_skip > 0) {
                (*inout_skip)--;
            } else if (*inout_y < max_lines) {
                tui_move_cursor(x, *inout_y);
                tui_set_color(sender_color, tui->config.theme.color_bg);
                printf("%s", indent);
                tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);
                (*inout_y)++;
            }
            break;
        }
    }
}

void tui_draw_sidebar(tui_t* tui) {
    if (!tui) return;
    const uint16_t top_h = 3;
    const uint16_t status_h = 3;
    const uint16_t input_h = 3;
    const uint16_t content_h = (tui->config.height > (top_h + status_h + input_h)) ?
        (uint16_t)(tui->config.height - top_h - status_h - input_h) : 0;
    if (content_h < 3) return;
    uint16_t sidebar_w = (tui->config.width > 120) ? 32 : 28;
    if (sidebar_w + 12 > tui->config.width) {
        sidebar_w = (uint16_t)(tui->config.width / 3);
    }
    if (sidebar_w < 18) sidebar_w = 18;
    uint16_t sidebar_h = content_h;
    uint16_t sidebar_y = top_h;

    const char* title = (tui->active_panel == TUI_PANEL_SIDEBAR) ? "Sessions (*)" : "Sessions";
    tui_set_color(tui->config.theme.color_secondary, tui->config.theme.color_bg);
    tui_draw_box(0, sidebar_y, sidebar_w, sidebar_h, title);

    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);

    // List sessions from agent
    uint32_t session_count = tui->agent ? tui->agent->ctx->session_count : 0;
    uint32_t max_rows = (sidebar_h > 4) ? sidebar_h - 4 : 0;
    uint32_t max_sessions = max_rows / 2;

    tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
    tui_move_cursor(2, (uint16_t)(sidebar_y + 1));
    printf("Total: %u", session_count);

    for (uint32_t i = 0; i < max_sessions; i++) {
        uint16_t row1 = (uint16_t)(sidebar_y + 2 + i * 2);
        uint16_t row2 = (uint16_t)(row1 + 1);
        tui_move_cursor(2, row1);

        bool is_selected = (i == tui->selected_session);
        bool is_active = false;
        agent_session_t* session = NULL;

        if (i < session_count && tui->agent && tui->agent->ctx) {
            session = tui->agent->ctx->sessions[i];
            is_active = (tui->agent->ctx->active_session == tui->agent->ctx->sessions[i]);
        }

        // Highlight selected session row
        if (is_selected && tui->active_panel == TUI_PANEL_SIDEBAR) {
            tui_set_color(tui->config.theme.color_bg, tui->config.theme.color_primary);
        } else if (is_active) {
            tui_set_color(tui->config.theme.color_success, tui->config.theme.color_bg);
        } else {
            tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
        }

        if (i < session_count && session) {
            const char* status_icon = tui_session_status_icon(tui, session, is_active);
            const char* name = session->name.data ? session->name.data : "unnamed";
            printf("%s %s", status_icon, name);

            char preview[160];
            tui_session_preview(session, preview, sizeof(preview));
            tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
            tui_draw_text_truncated(4, row2, (uint16_t)(sidebar_w > 6 ? sidebar_w - 6 : 1), preview);
        } else if (i == 0 && session_count == 0) {
            printf("  (no sessions)");
            tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
            tui_move_cursor(4, row2);
            printf("Ctrl+N to create");
        } else {
            break;
        }
    }

    tui_reset_color();
}

void tui_draw_chat_panel(tui_t* tui) {
    if (!tui) return;
    const uint16_t top_h = 3;
    const uint16_t status_h = 3;
    const uint16_t input_h = 3;
    const uint16_t content_h = (tui->config.height > (top_h + status_h + input_h)) ?
        (uint16_t)(tui->config.height - top_h - status_h - input_h) : 0;
    if (content_h < 3) return;
    uint16_t sidebar_w = (tui->config.width > 120) ? 32 : 28;
    if (sidebar_w + 12 > tui->config.width) {
        sidebar_w = (uint16_t)(tui->config.width / 3);
    }
    if (sidebar_w < 18) sidebar_w = 18;
    uint16_t x = sidebar_w;
    uint16_t y = top_h;
    uint16_t w = tui->config.width > sidebar_w ? (uint16_t)(tui->config.width - sidebar_w) : 0;
    uint16_t h = content_h;
    if (w < 10) return;

    tui_set_color(tui->config.theme.color_secondary, tui->config.theme.color_bg);
    tui_draw_box(x, y, w, h, "Conversation");

    // Chat content area - render messages
    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);

    // Calculate visible message area
    uint16_t max_lines = h - 2;
    uint16_t draw_y = y + 1;
    uint16_t draw_limit = (uint16_t)(y + 1 + max_lines);
    uint16_t text_w = (w > 4) ? (uint16_t)(w - 4) : 1;

    int32_t view_session_index = tui_get_view_session_index(tui);
    if (view_session_index >= 0) {
        tui_hydrate_session_messages(tui, view_session_index);
    }

    // Show placeholder if no messages
    if (!tui->messages) {
        const char* placeholder[] = {
            "Welcome to CClaw Agent!",
            "Type a message to start chatting.",
            "Use /help for commands.",
            NULL
        };
        for (int i = 0; placeholder[i] && i < (int)max_lines; i++) {
            tui_move_cursor(x + 2, (uint16_t)(draw_y + i));
            printf("%s", placeholder[i]);
        }
    } else {
        // Render wrapped messages from linked list
        tui_message_t* msg = tui->messages;
        bool has_visible = false;

        uint32_t total_lines = 0;
        while (msg) {
            if (tui_message_visible_for_session(msg, view_session_index)) {
                has_visible = true;
                total_lines += tui_wrapped_lines_for_message(msg, text_w);
            }
            msg = msg->next;
        }

        if (!has_visible) {
            tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
            tui_move_cursor((uint16_t)(x + 2), draw_y);
            printf("No messages in this session yet.");
            tui_move_cursor((uint16_t)(x + 2), (uint16_t)(draw_y + 1));
            printf("Type to start a new thread in this session.");
            tui_reset_color();
            return;
        }

        uint32_t base_skip = (total_lines > max_lines) ? total_lines - max_lines : 0;
        if (tui->scroll_offset > base_skip) {
            tui->scroll_offset = base_skip;
        }
        uint32_t skip = (base_skip > tui->scroll_offset) ? base_skip - tui->scroll_offset : 0;
        msg = tui->messages;

        while (msg && draw_y < draw_limit) {
            if (tui_message_visible_for_session(msg, view_session_index)) {
                tui_render_wrapped_message(tui, msg, (uint16_t)(x + 2), &draw_y, text_w, draw_limit, &skip);
                if (skip > 0) {
                    skip--;
                } else if (draw_y < draw_limit) {
                    tui_move_cursor((uint16_t)(x + 2), draw_y);
                    tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
                    for (uint16_t i = 0; i < text_w; i++) {
                        printf("%s", (i == 0) ? "·" : " ");
                    }
                    draw_y++;
                }
            }
            msg = msg->next;
        }
    }

    tui_reset_color();
}

void tui_draw_status_bar(tui_t* tui) {
    if (!tui || tui->config.height < 6) return;
    uint16_t y = tui->config.height - 6;
    tui_set_color(tui->config.theme.color_secondary, tui->config.theme.color_bg);
    tui_draw_box(0, y, tui->config.width, 3, "Runtime");

    char status[256];
    const char* model_name = "unknown";
    if (tui->agent && tui->agent->ctx && tui->agent->ctx->provider) {
        model_name = tui->agent->ctx->provider->config.default_model.data;
        if (!model_name) model_name = "unknown";
    }
    const char* run_state = "idle";
    if (tui->zc_turn_cancelling) {
        run_state = "cancelling";
    } else if (tui->zc_turn_inflight) {
        run_state = "running";
    }

    const char* spinner = " ";
    if (tui->zc_turn_inflight) {
        static const char* frames[] = {"|", "/", "-", "\\"};
        spinner = frames[(unsigned)time(NULL) % 4];
    }

    snprintf(
        status,
        sizeof(status),
        "Engine: %s | Model: %s | State: %s %s | Theme: %s | Tools: %s | Scroll: %u",
        (tui->use_zeroclaw_session && tui->zc_session) ? "zeroclaw-session" : "legacy-c-agent",
        model_name,
        run_state,
        spinner,
        tui_theme_mode_name(tui->theme_mode),
        tui->show_tool_details ? "full" : "summary",
        tui->scroll_offset
    );

    if (tui->zc_turn_cancelling) {
        tui_set_color(tui->config.theme.color_warning, tui->config.theme.color_bg);
    } else if (tui->zc_turn_inflight) {
        tui_set_color(tui->config.theme.color_success, tui->config.theme.color_bg);
    } else {
        tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);
    }
    tui_move_cursor(2, (uint16_t)(y + 1));
    printf("%s", status);

    tui_reset_color();
}

void tui_draw_input_area(tui_t* tui) {
    if (!tui || tui->config.height < 3) return;
    uint16_t y = tui->config.height - 3;

    tui_set_color(tui->config.theme.color_secondary, tui->config.theme.color_bg);
    tui_draw_box(0, y, tui->config.width, 3, "Input");

    tui_set_color(tui->config.theme.color_success, tui->config.theme.color_bg);
    tui_move_cursor(2, (uint16_t)(y + 1));
    printf(">");

    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);
    tui_move_cursor(4, (uint16_t)(y + 1));
    tui_draw_text_truncated(4, (uint16_t)(y + 1), (uint16_t)(tui->config.width > 8 ? tui->config.width - 8 : 1), tui->input_buffer);

    if (tui->config.width > 30) {
        const char* hint = "Ready";
        uint8_t hint_color = tui->config.theme.color_success;
        if (tui->zc_turn_cancelling) {
            hint = "Cancelling";
            hint_color = tui->config.theme.color_warning;
        } else if (tui->zc_turn_inflight) {
            hint = "Running";
            hint_color = tui->config.theme.color_secondary;
        }
        size_t hint_len = strlen(hint);
        uint16_t hint_x = (uint16_t)(tui->config.width - (uint16_t)hint_len - 2);
        if (hint_x > 6) {
            tui_set_color(hint_color, tui->config.theme.color_bg);
            tui_move_cursor(hint_x, (uint16_t)(y + 1));
            printf("%s", hint);
        }
    }

    uint16_t cursor_x = (uint16_t)(4 + tui->input_pos);
    if (cursor_x >= tui->config.width - 1) {
        cursor_x = tui->config.width - 2;
    }
    tui_move_cursor(cursor_x, (uint16_t)(y + 1));

    tui_reset_color();
}

// ============================================================================
// Input Handling
// ============================================================================

err_t tui_process_input(tui_t* tui) {
    if (!tui) return ERR_INVALID_ARGUMENT;

    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    if (n <= 0) {
        return ERR_OK; // No input
    }

    // Handle escape sequences
    if (c == '\033') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return ERR_OK;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return ERR_OK;

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': // Up arrow
                    if (tui->active_panel == TUI_PANEL_SIDEBAR) {
                        // Navigate up in session list
                        if (tui->selected_session > 0) {
                            tui->selected_session--;
                        }
                    } else {
                        // Normal history navigation
                        const char* hist = tui_history_prev(tui);
                        if (hist) {
                            size_t hist_len = strlen(hist);
                            if (hist_len >= tui->input_capacity) {
                                hist_len = tui->input_capacity - 1;
                            }
                            memcpy(tui->input_buffer, hist, hist_len);
                            tui->input_buffer[hist_len] = '\0';
                            tui->input_len = (uint32_t)hist_len;
                            tui->input_pos = tui->input_len;
                        }
                    }
                    break;
                case 'B': // Down arrow
                    if (tui->active_panel == TUI_PANEL_SIDEBAR) {
                        // Navigate down in session list
                        if (tui->agent && tui->selected_session + 1 < tui->agent->ctx->session_count) {
                            tui->selected_session++;
                        }
                    } else {
                        // Normal history navigation
                        const char* hist = tui_history_next(tui);
                        if (hist) {
                            size_t hist_len = strlen(hist);
                            if (hist_len >= tui->input_capacity) {
                                hist_len = tui->input_capacity - 1;
                            }
                            memcpy(tui->input_buffer, hist, hist_len);
                            tui->input_buffer[hist_len] = '\0';
                            tui->input_len = (uint32_t)hist_len;
                            tui->input_pos = tui->input_len;
                        } else {
                            tui_input_clear(tui);
                        }
                    }
                    break;
                case 'C': tui_input_move_right(tui); break; // Right
                case 'D': tui_input_move_left(tui); break;  // Left
                case 'H': tui_input_move_home(tui); break;  // Home
                case 'F': tui_input_move_end(tui); break;   // End
                case '3': // Delete
                    read(STDIN_FILENO, &c, 1); // consume ~
                    tui_input_delete(tui);
                    break;
                case '5': // PageUp
                    read(STDIN_FILENO, &c, 1); // consume ~
                    tui_chat_scroll_up(tui, 5);
                    break;
                case '6': // PageDown
                    read(STDIN_FILENO, &c, 1); // consume ~
                    tui_chat_scroll_down(tui, 5);
                    break;
            }
        }
        tui->needs_redraw = true;
        return ERR_OK;
    }

    // Handle control characters
    if (c == TUI_KEY_CTRL('q')) {
        tui->running = false;
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('c')) {
        if (tui->use_zeroclaw_session && tui->zc_session && tui->zc_turn_inflight) {
            if (!tui->zc_turn_cancelling) {
                zc_session_cancel(tui->zc_session, tui->zc_active_turn_id);
                tui->zc_turn_cancelling = true;
                tui_chat_add_system_message(tui, "Cancelling current turn...");
            } else {
                tui_chat_add_system_message(tui, "Cancellation already requested...");
            }
            tui->needs_redraw = true;
        } else {
            tui->running = false;
        }
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('h')) {
        tui_chat_add_system_message(tui, "Help: Ctrl+N new, Ctrl+B branch, Ctrl+T theme, Ctrl+O tools, PgUp/PgDn scroll, Ctrl+C cancel, Ctrl+Q quit");
        tui->needs_redraw = true;
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('o')) {
        tui->show_tool_details = !tui->show_tool_details;
        tui_chat_add_system_message(tui, tui->show_tool_details ? "Tool detail mode: full" : "Tool detail mode: summary");
        tui->needs_redraw = true;
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('t')) {
        tui_apply_theme_mode(tui, (uint8_t)(tui->theme_mode + 1));
        char info[96];
        snprintf(info, sizeof(info), "Theme switched to %s", tui_theme_mode_name(tui->theme_mode));
        tui_chat_add_system_message(tui, info);
        tui->needs_redraw = true;
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('n')) {
        // Create new session
        if (tui->agent) {
            char name_buf[64];
            snprintf(name_buf, sizeof(name_buf), "session-%u", tui->agent->ctx->session_count + 1);
            str_t session_name = str_dup_cstr(name_buf, NULL);
            agent_session_t* new_session = NULL;
            err_t err = agent_session_create(tui->agent, &session_name, &new_session);
            if (err == ERR_OK && new_session) {
                // Copy model from active session or use default
                if (tui->agent->ctx->active_session && !str_empty(tui->agent->ctx->active_session->model)) {
                    new_session->model = str_dup(tui->agent->ctx->active_session->model, NULL);
                }
                tui->agent->ctx->active_session = new_session;
                tui_chat_add_system_message(tui, "Created new session");
            } else {
                tui_chat_add_system_message(tui, "Error: Failed to create session");
            }
            free((void*)session_name.data);
            tui->needs_redraw = true;
        }
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('b')) {
        // Create new branch (similar to new session but with branch semantics)
        if (tui->agent && tui->agent->ctx->active_session) {
            char name_buf[64];
            snprintf(name_buf, sizeof(name_buf), "branch-%u", tui->agent->ctx->session_count + 1);
            str_t branch_name = str_dup_cstr(name_buf, NULL);
            // For now, just create a new session as branch
            agent_session_t* new_branch = NULL;
            err_t err = agent_session_create(tui->agent, &branch_name, &new_branch);
            if (err == ERR_OK && new_branch) {
                if (!str_empty(tui->agent->ctx->active_session->model)) {
                    new_branch->model = str_dup(tui->agent->ctx->active_session->model, NULL);
                }
                tui->agent->ctx->active_session = new_branch;
                tui_chat_add_system_message(tui, "Created new branch");
            } else {
                tui_chat_add_system_message(tui, "Error: Failed to create branch");
            }
            free((void*)branch_name.data);
            tui->needs_redraw = true;
        } else {
            tui_chat_add_system_message(tui, "Error: No active session to branch from");
        }
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('l')) {
        tui_redraw(tui);
        return ERR_OK;
    }

    // Handle Tab key to switch panels
    if (c == TUI_KEY_TAB) {
        tui->active_panel = (tui->active_panel == TUI_PANEL_CHAT) ? TUI_PANEL_SIDEBAR : TUI_PANEL_CHAT;
        tui->needs_redraw = true;
        return ERR_OK;
    }

    // Handle regular input
    switch (c) {
        case '\r':
        case '\n':
            // If in sidebar, activate selected session
            if (tui->active_panel == TUI_PANEL_SIDEBAR) {
                if (tui->agent && tui->selected_session < tui->agent->ctx->session_count) {
                    tui->agent->ctx->active_session = tui->agent->ctx->sessions[tui->selected_session];
                    tui_chat_add_system_message(tui, "Switched session");
                    tui->needs_redraw = true;
                }
                return ERR_OK;
            }
            
            // Submit input
            if (tui->input_len > 0) {
                tui_history_add(tui, tui->input_buffer);
                tui_chat_add_user_message(tui, tui->input_buffer);
                
                // Process message via ZeroClaw session bridge if available
                if (tui->use_zeroclaw_session && tui->zc_session) {
                    if (tui->zc_turn_inflight) {
                        tui_chat_add_system_message(tui, "Turn is running. Press Ctrl+C to cancel.");
                    } else {
                        uint64_t turn_id = 0;
                        zc_result_t send_res = zc_session_send(tui->zc_session, tui->input_buffer, &turn_id);
                        if (send_res == ZC_OK) {
                            tui->zc_turn_inflight = true;
                            tui->zc_turn_cancelling = false;
                            tui->zc_active_turn_id = turn_id;
                        } else {
                            tui_chat_add_system_message(tui, "Error: Failed to submit ZeroClaw message");
                        }
                    }
                } else if (tui->agent && tui->agent->ctx && tui->agent->ctx->provider) {
                    // Legacy fallback path
                    str_t user_input = str_dup_cstr(tui->input_buffer, NULL);
                    str_t response = STR_NULL;
                    agent_session_t* session = NULL;
                    if (tui->agent->ctx->active_session) {
                        session = tui->agent->ctx->active_session;
                    } else if (tui->agent->ctx->session_count > 0) {
                        session = tui->agent->ctx->sessions[0];
                    }
                    if (session) {
                        err_t err = agent_process_message(tui->agent, session, &user_input, &response);
                        if (err == ERR_OK && response.data) {
                            tui_chat_add_assistant_message(tui, response.data);
                            free((void*)response.data);
                        } else {
                            tui_chat_add_system_message(tui, "Error: Failed to get response");
                        }
                    } else {
                        tui_chat_add_system_message(tui, "Error: No active session");
                    }
                    free((void*)user_input.data);
                } else {
                    tui_chat_add_system_message(tui, "Warning: No provider configured");
                }
                
                tui_input_clear(tui);
            }
            break;
        case TUI_KEY_BACKSPACE:
            tui_input_backspace(tui);
            break;
        case TUI_KEY_CTRL('a'):
        case TUI_KEY_ESC:
            tui_input_move_home(tui);
            break;
        case TUI_KEY_CTRL('e'):
            tui_input_move_end(tui);
            break;
        case TUI_KEY_CTRL('u'):
            tui_input_clear(tui);
            break;
        default:
            // Handle UTF-8 input
            {
                unsigned char uc = (unsigned char)c;
                // Check if this is a valid UTF-8 start byte or ASCII
                if ((uc & 0x80) == 0) {
                    // ASCII character (0xxxxxxx)
                    if (isprint(c) || c == ' ') {
                        tui_input_insert(tui, c);
                    }
                } else if ((uc & 0xE0) == 0xC0) {
                    // 2-byte UTF-8 sequence (110xxxxx)
                    char utf8_seq[3] = {c, 0, 0};
                    if (read(STDIN_FILENO, &utf8_seq[1], 1) == 1) {
                        tui_input_insert(tui, utf8_seq[0]);
                        tui_input_insert(tui, utf8_seq[1]);
                    }
                } else if ((uc & 0xF0) == 0xE0) {
                    // 3-byte UTF-8 sequence (1110xxxx) - Chinese characters
                    char utf8_seq[4] = {c, 0, 0, 0};
                    if (read(STDIN_FILENO, &utf8_seq[1], 1) == 1 &&
                        read(STDIN_FILENO, &utf8_seq[2], 1) == 1) {
                        tui_input_insert(tui, utf8_seq[0]);
                        tui_input_insert(tui, utf8_seq[1]);
                        tui_input_insert(tui, utf8_seq[2]);
                    }
                } else if ((uc & 0xF8) == 0xF0) {
                    // 4-byte UTF-8 sequence (11110xxx)
                    char utf8_seq[5] = {c, 0, 0, 0, 0};
                    if (read(STDIN_FILENO, &utf8_seq[1], 1) == 1 &&
                        read(STDIN_FILENO, &utf8_seq[2], 1) == 1 &&
                        read(STDIN_FILENO, &utf8_seq[3], 1) == 1) {
                        tui_input_insert(tui, utf8_seq[0]);
                        tui_input_insert(tui, utf8_seq[1]);
                        tui_input_insert(tui, utf8_seq[2]);
                        tui_input_insert(tui, utf8_seq[3]);
                    }
                }
                // Invalid UTF-8 bytes are ignored
            }
            break;
    }

    tui->needs_redraw = true;
    return ERR_OK;
}

// ============================================================================
// UTF-8 Helper Functions
// ============================================================================

// Check if a byte is a UTF-8 continuation byte (10xxxxxx)
static bool is_utf8_continuation(char c) {
    return (c & 0xC0) == 0x80;
}

// Get the length of a UTF-8 character starting at position pos
static uint32_t utf8_char_len(const char* str, uint32_t pos) {
    unsigned char c = (unsigned char)str[pos];
    if ((c & 0x80) == 0) return 1;        // 0xxxxxxx - ASCII (1 byte)
    if ((c & 0xE0) == 0xC0) return 2;    // 110xxxxx - 2 bytes
    if ((c & 0xF0) == 0xE0) return 3;    // 1110xxxx - 3 bytes (Chinese)
    if ((c & 0xF8) == 0xF0) return 4;    // 11110xxx - 4 bytes
    return 1; // Invalid UTF-8, treat as single byte
}

// Find the start position of the previous UTF-8 character
static uint32_t utf8_prev_char(const char* str, uint32_t pos) {
    if (pos == 0) return 0;
    // Move back until we find a non-continuation byte
    do {
        pos--;
    } while (pos > 0 && is_utf8_continuation(str[pos]));
    return pos;
}

// Count UTF-8 characters (not bytes) from start to end
static uint32_t utf8_char_count(const char* str, uint32_t len) {
    uint32_t count = 0;
    uint32_t i = 0;
    while (i < len) {
        i += utf8_char_len(str, i);
        count++;
    }
    return count;
}

// ============================================================================
// Input Buffer Operations
// ============================================================================

void tui_input_clear(tui_t* tui) {
    if (!tui) return;
    tui->input_buffer[0] = '\0';
    tui->input_len = 0;
    tui->input_pos = 0;
}

void tui_input_insert(tui_t* tui, char c) {
    if (!tui || tui->input_len >= tui->input_capacity - 1) return;

    // Make room for new character
    for (uint32_t i = tui->input_len; i > tui->input_pos; i--) {
        tui->input_buffer[i] = tui->input_buffer[i - 1];
    }

    tui->input_buffer[tui->input_pos] = c;
    tui->input_len++;
    tui->input_pos++;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_backspace(tui_t* tui) {
    if (!tui || tui->input_pos == 0) return;

    // Find the start of the previous UTF-8 character
    uint32_t prev_pos = utf8_prev_char(tui->input_buffer, tui->input_pos);
    uint32_t char_len = tui->input_pos - prev_pos;

    // Shift remaining characters left by char_len bytes
    for (uint32_t i = prev_pos; i < tui->input_len - char_len; i++) {
        tui->input_buffer[i] = tui->input_buffer[i + char_len];
    }

    tui->input_len -= char_len;
    tui->input_pos = prev_pos;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_delete(tui_t* tui) {
    if (!tui || tui->input_pos >= tui->input_len) return;

    // Get the length of the UTF-8 character at current position
    uint32_t char_len = utf8_char_len(tui->input_buffer, tui->input_pos);

    // Shift remaining characters left
    for (uint32_t i = tui->input_pos; i < tui->input_len - char_len; i++) {
        tui->input_buffer[i] = tui->input_buffer[i + char_len];
    }

    tui->input_len -= char_len;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_move_left(tui_t* tui) {
    if (tui && tui->input_pos > 0) {
        // Move to the start of the previous UTF-8 character
        tui->input_pos = utf8_prev_char(tui->input_buffer, tui->input_pos);
    }
}

void tui_input_move_right(tui_t* tui) {
    if (tui && tui->input_pos < tui->input_len) {
        // Move to the next UTF-8 character
        tui->input_pos += utf8_char_len(tui->input_buffer, tui->input_pos);
    }
}

void tui_input_move_home(tui_t* tui) {
    if (tui) {
        tui->input_pos = 0;
    }
}

void tui_input_move_end(tui_t* tui) {
    if (tui) {
        tui->input_pos = tui->input_len;
    }
}

const char* tui_input_get(tui_t* tui) {
    return tui ? tui->input_buffer : NULL;
}

// ============================================================================
// History
// ============================================================================

void tui_history_add(tui_t* tui, const char* entry) {
    if (!tui || !entry || strlen(entry) == 0) return;

    // Don't add duplicates
    if (tui->history_count > 0 && strcmp(tui->history[0], entry) == 0) {
        return;
    }

    // Shift history
    if (tui->history_count >= tui->history_capacity) {
        free(tui->history[tui->history_capacity - 1]);
        tui->history_count--;
    }

    for (uint32_t i = tui->history_count; i > 0; i--) {
        tui->history[i] = tui->history[i - 1];
    }

    tui->history[0] = strdup(entry);
    tui->history_count++;
    tui->history_pos = (uint32_t)-1;
}

const char* tui_history_prev(tui_t* tui) {
    if (!tui || tui->history_count == 0) return NULL;

    if (tui->history_pos + 1 < tui->history_count) {
        tui->history_pos++;
        return tui->history[tui->history_pos];
    }

    return NULL;
}

const char* tui_history_next(tui_t* tui) {
    if (!tui || tui->history_count == 0) return NULL;

    if (tui->history_pos > 0) {
        tui->history_pos--;
        return tui->history[tui->history_pos];
    }

    tui->history_pos = (uint32_t)-1;
    return NULL;
}

// ============================================================================
// Chat Display
// ============================================================================

void tui_chat_add_message(tui_t* tui, agent_message_t* message) {
    if (!tui || !message) return;
    const char* sender = "system";
    if (message->type == AGENT_MSG_USER) {
        sender = "user";
    } else if (message->type == AGENT_MSG_ASSISTANT) {
        sender = "assistant";
    }
    const char* text = (message->content.data && message->content.len > 0) ? message->content.data : "";
    tui_chat_add_message_internal_ex(tui, sender, text, tui_get_active_session_index(tui), false);
    tui->needs_redraw = true;
}

void tui_chat_add_system_message(tui_t* tui, const char* text) {
    tui_chat_add_message_internal_ex(tui, "system", text, -2, true);
    if (tui) tui->needs_redraw = true;
}

void tui_chat_add_user_message(tui_t* tui, const char* text) {
    tui_chat_add_message_internal_ex(tui, "user", text, -2, true);
    if (tui) tui->scroll_offset = 0;
    if (tui) tui->needs_redraw = true;
}

void tui_chat_add_assistant_message(tui_t* tui, const char* text) {
    tui_chat_add_message_internal_ex(tui, "assistant", text, -2, true);
    if (tui) tui->scroll_offset = 0;
    if (tui) tui->needs_redraw = true;
}

void tui_chat_add_tool_call(tui_t* tui, const char* tool_name, const char* args) {
    if (!tui || !tool_name) return;
    char line[512];
    tui_format_tool_text(line, sizeof(line), "start", tool_name, args);
    tui_chat_add_message_internal_ex(tui, "tool_call", line, -2, false);
    if (tui->use_zeroclaw_session) {
        tui_session_append_tree_message(tui, AGENT_MSG_TOOL_CALL, line, tool_name, args, NULL);
    }
    tui->needs_redraw = true;
}

void tui_chat_add_tool_result(tui_t* tui, const char* tool_name, const char* result) {
    if (!tui || !tool_name) return;
    char line[512];
    tui_format_tool_text(line, sizeof(line), "end", tool_name, result);
    tui_chat_add_message_internal_ex(tui, "tool_result", line, -2, false);
    if (tui->use_zeroclaw_session) {
        tui_session_append_tree_message(tui, AGENT_MSG_TOOL_RESULT, line, tool_name, NULL, result);
    }
    tui->needs_redraw = true;
}

void tui_chat_scroll_up(tui_t* tui, uint32_t lines) {
    if (!tui) return;
    tui->scroll_offset += lines;
    tui->needs_redraw = true;
}

void tui_chat_scroll_down(tui_t* tui, uint32_t lines) {
    if (!tui) return;
    if (lines >= tui->scroll_offset) {
        tui->scroll_offset = 0;
    } else {
        tui->scroll_offset -= lines;
    }
    tui->needs_redraw = true;
}
