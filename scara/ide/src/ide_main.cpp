#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>
#include <commdlg.h>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

struct OpenFileTab {
    std::string path;
    std::string name;
    std::string content;
    bool open = true;
    bool dirty = false;
    int focus_line = -1;
    int syntax_scroll_line = -1;
    std::vector<std::string> syntax_lines;
    bool syntax_cache_dirty = true;
};

struct DiagnosticItem {
    std::string file_path;
    int line = -1;
    std::string kind;
    std::string message;
};

struct SymbolItem {
    std::string kind;
    std::string name;
    int line = -1;
};

struct EditorUiState {
    int cursor_pos = 0;
    int line = 1;
    int col = 1;
    bool show_editor_syntax = true;
    float editor_syntax_split = 0.46f;
    bool show_find = false;
    bool show_goto = false;
    std::string find_query;
    std::vector<int> find_hits;
    int find_index = -1;
    std::string goto_line_input;
    bool show_goto_def = false;
    std::string goto_def_symbol;
    bool show_refs = false;
    std::string refs_symbol;
    std::vector<int> refs_hits;
    int refs_index = -1;
    bool show_symbols = true;
    std::string symbol_filter;
    bool show_symbol_info = true;
};

struct DiagnosticsFilterState {
    bool show_errors = true;
    bool show_warnings = true;
    bool show_info = true;
};

struct ProblemsPanelState {
    std::string search_query;
    int sort_mode = 0;  // 0=archivo/linea, 1=severidad, 2=mensaje
};

struct CursorCaptureData {
    int* cursor_pos;
};

static bool is_word_char(char c);
static std::string to_upper_copy(std::string s);

static bool is_ident_start(char c) {
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool is_scara_keyword_upper(const std::string& word_upper) {
    static const char* kwords[] = {
        "PROGRAM", "END", "VAR", "POINT", "MOVE", "MOVEJ", "APPROACH", "DEPART",
        "HOME", "OPEN", "CLOSE", "SPEED", "WAIT", "IF", "ELSE", "END_IF",
        "WHILE", "END_WHILE", "REPEAT", "END_REPEAT", "PRINT", "HALT"
    };
    for (const char* kw : kwords) {
        if (word_upper == kw) return true;
    }
    return false;
}

static bool is_numeric_start(const std::string& line, int pos) {
    if (pos < 0 || pos >= static_cast<int>(line.size())) return false;
    char c = line[pos];
    if (c >= '0' && c <= '9') return true;
    if ((c == '-' || c == '+') && pos + 1 < static_cast<int>(line.size())) {
        char n = line[pos + 1];
        if (n < '0' || n > '9') return false;
        if (pos == 0) return true;
        char p = line[pos - 1];
        return !is_ident_char(p);
    }
    return false;
}

static void draw_inline_token_colored(const std::string& token, const ImVec4& color) {
    if (token.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(token.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);
}

static void draw_scara_line_highlighted(const std::string& line, int line_no, int focus_line, int cursor_line) {
    const ImVec4 col_default = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 col_gutter = ImVec4(0.36f, 0.40f, 0.45f, 1.0f);
    const ImVec4 col_focus = ImVec4(0.82f, 0.24f, 0.16f, 1.0f);
    const ImVec4 col_cursor = ImVec4(0.12f, 0.46f, 0.78f, 1.0f);
    const ImVec4 col_keyword = ImVec4(0.06f, 0.33f, 0.75f, 1.0f);
    const ImVec4 col_comment = ImVec4(0.14f, 0.56f, 0.22f, 1.0f);
    const ImVec4 col_string = ImVec4(0.68f, 0.38f, 0.08f, 1.0f);
    const ImVec4 col_number = ImVec4(0.48f, 0.26f, 0.72f, 1.0f);

    std::string prefix = (line_no < 10 ? "   " : (line_no < 100 ? "  " : (line_no < 1000 ? " " : ""))) +
                         std::to_string(line_no) + " | ";
    ImVec4 gutter_col = col_gutter;
    if (focus_line == line_no) {
        gutter_col = col_focus;
    } else if (cursor_line == line_no) {
        gutter_col = col_cursor;
    }
    draw_inline_token_colored(prefix, gutter_col);

    int i = 0;
    const int n = static_cast<int>(line.size());
    while (i < n) {
        char c = line[i];

        if (c == '#') {
            draw_inline_token_colored(line.substr(static_cast<size_t>(i)), col_comment);
            i = n;
            break;
        }

        if (c == '"') {
            int j = i + 1;
            while (j < n) {
                if (line[j] == '"' && line[j - 1] != '\\') {
                    j++;
                    break;
                }
                j++;
            }
            draw_inline_token_colored(line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i)), col_string);
            i = j;
            continue;
        }

        if (is_numeric_start(line, i)) {
            int j = i + 1;
            bool seen_dot = false;
            while (j < n) {
                char cj = line[j];
                if (cj == '.' && !seen_dot) {
                    seen_dot = true;
                    j++;
                    continue;
                }
                if (cj < '0' || cj > '9') break;
                j++;
            }
            draw_inline_token_colored(line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i)), col_number);
            i = j;
            continue;
        }

        if (is_ident_start(c)) {
            int j = i + 1;
            while (j < n && is_ident_char(line[j])) j++;
            std::string word = line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i));
            std::string up = to_upper_copy(word);
            draw_inline_token_colored(word, is_scara_keyword_upper(up) ? col_keyword : col_default);
            i = j;
            continue;
        }

        int j = i + 1;
        while (j < n && line[j] != '#' && line[j] != '"' &&
               !is_numeric_start(line, j) && !is_ident_start(line[j])) {
            j++;
        }
        draw_inline_token_colored(line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i)), col_default);
        i = j;
    }

    ImGui::NewLine();
}

static void draw_scara_highlighted_view(const char* child_id,
                                        const std::vector<std::string>& lines,
                                        int focus_line,
                                        int cursor_line,
                                        int scroll_line,
                                        int max_lines,
                                        bool draw_title) {
    ImGui::BeginChild(child_id, ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (draw_title) {
        ImGui::TextUnformatted("Vista rapida SCARA");
        ImGui::Separator();
    }

    int total_lines = static_cast<int>(lines.size());
    if (max_lines > 0 && total_lines > max_lines) {
        total_lines = max_lines;
    }

    if (scroll_line > 0 && total_lines > 0) {
        int idx = scroll_line - 1;
        if (idx < 0) idx = 0;
        if (idx >= total_lines) idx = total_lines - 1;
        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        float target = static_cast<float>(idx) * line_h;
        float max_scroll = ImGui::GetScrollMaxY();
        if (target > max_scroll) target = max_scroll;
        if (target < 0.0f) target = 0.0f;
        ImGui::SetScrollY(target);
    }

    ImGuiListClipper clipper;
    clipper.Begin(total_lines);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            draw_scara_line_highlighted(lines[i], i + 1, focus_line, cursor_line);
        }
    }

    ImGui::EndChild();
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static int imgui_capture_cursor_cb(ImGuiInputTextCallbackData* data) {
    CursorCaptureData* d = reinterpret_cast<CursorCaptureData*>(data->UserData);
    if (d && d->cursor_pos) {
        *d->cursor_pos = data->CursorPos;
    }
    return 0;
}

static std::string trim_copy(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

static void compute_line_col(const std::string& text, int cursor_pos, int& out_line, int& out_col) {
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > static_cast<int>(text.size())) cursor_pos = static_cast<int>(text.size());

    int line = 1;
    int col = 1;
    for (int i = 0; i < cursor_pos; ++i) {
        if (text[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    out_line = line;
    out_col = col;
}

static int line_from_offset(const std::string& text, int offset) {
    if (offset < 0) offset = 0;
    if (offset > static_cast<int>(text.size())) offset = static_cast<int>(text.size());
    int line = 1;
    for (int i = 0; i < offset; ++i) {
        if (text[i] == '\n') line++;
    }
    return line;
}

static void set_tab_focus_line(OpenFileTab& tab, int line) {
    if (line <= 0) return;
    tab.focus_line = line;
    tab.syntax_scroll_line = line;
}

static void rebuild_syntax_cache(OpenFileTab& tab) {
    if (!tab.syntax_cache_dirty) return;

    tab.syntax_lines.clear();
    tab.syntax_lines.reserve(256);

    const std::string& text = tab.content;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            size_t end = i;
            if (end > start && text[end - 1] == '\r') end--;
            tab.syntax_lines.push_back(text.substr(start, end - start));
            start = i + 1;
        }
    }
    if (start < text.size()) {
        size_t end = text.size();
        if (end > start && text[end - 1] == '\r') end--;
        tab.syntax_lines.push_back(text.substr(start, end - start));
    }

    tab.syntax_cache_dirty = false;
}

static std::string word_at_cursor(const std::string& text, int cursor_pos) {
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > static_cast<int>(text.size())) cursor_pos = static_cast<int>(text.size());
    if (text.empty()) return "";

    int left = cursor_pos;
    if (left > 0 && !is_word_char(text[left - 1]) && left < static_cast<int>(text.size()) && is_word_char(text[left])) {
        left++;
    }
    while (left > 0 && is_word_char(text[left - 1])) left--;

    int right = cursor_pos;
    while (right < static_cast<int>(text.size()) && is_word_char(text[right])) right++;

    if (right <= left) return "";
    return text.substr(static_cast<size_t>(left), static_cast<size_t>(right - left));
}

static std::vector<int> find_all_hits(const std::string& text, const std::string& query) {
    std::vector<int> hits;
    if (query.empty()) return hits;

    size_t pos = 0;
    while (true) {
        pos = text.find(query, pos);
        if (pos == std::string::npos) break;
        hits.push_back(static_cast<int>(pos));
        pos += query.size();
    }
    return hits;
}

static bool is_word_char(char c) {
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static std::vector<int> find_symbol_usages_offsets(const std::string& text, const std::string& symbol) {
    std::vector<int> hits;
    if (symbol.empty()) return hits;

    std::string sym = to_upper_copy(symbol);
    const int n = static_cast<int>(text.size());
    const int m = static_cast<int>(sym.size());
    if (m <= 0 || m > n) return hits;

    for (int i = 0; i + m <= n; ++i) {
        bool at_start = (i == 0) || !is_word_char(text[i - 1]);
        bool at_end = (i + m == n) || !is_word_char(text[i + m]);
        if (!at_start || !at_end) continue;

        bool eq = true;
        for (int k = 0; k < m; ++k) {
            char c = text[i + k];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (c != sym[k]) {
                eq = false;
                break;
            }
        }
        if (eq) hits.push_back(i);
    }
    return hits;
}

static std::string to_upper_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
        return static_cast<char>(c);
    });
    return s;
}

static std::string current_word_prefix(const std::string& text, int cursor_pos, int& out_prefix_len) {
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > static_cast<int>(text.size())) cursor_pos = static_cast<int>(text.size());

    int i = cursor_pos - 1;
    while (i >= 0 && is_word_char(text[i])) i--;
    int start = i + 1;
    out_prefix_len = cursor_pos - start;
    if (out_prefix_len <= 0) {
        out_prefix_len = 0;
        return "";
    }
    return text.substr(start, out_prefix_len);
}

static void insert_completion_at_cursor(std::string& text,
                                        int& cursor_pos,
                                        const std::string& completion,
                                        int prefix_len) {
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > static_cast<int>(text.size())) cursor_pos = static_cast<int>(text.size());
    if (prefix_len < 0) prefix_len = 0;
    if (prefix_len > cursor_pos) prefix_len = cursor_pos;

    int start = cursor_pos - prefix_len;
    text.replace(static_cast<size_t>(start), static_cast<size_t>(prefix_len), completion);
    cursor_pos = start + static_cast<int>(completion.size());
}

static std::string file_name_from_path(const std::string& path) {
    std::filesystem::path p(path);
    return p.filename().string();
}

static std::string normalize_path(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

static std::string breadcrumb_from_path(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    std::string result;
    std::stringstream ss(out);
    std::string item;
    bool first = true;
    while (std::getline(ss, item, '/')) {
        if (item.empty()) continue;
        if (!first) result += " > ";
        result += item;
        first = false;
    }
    return result.empty() ? path : result;
}

static bool read_text_file(const std::string& path, std::string& out_text, std::string& out_error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out_error = "No se pudo abrir el archivo: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out_text = buffer.str();
    return true;
}

static bool write_text_file(const std::string& path, const std::string& text, std::string& out_error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        out_error = "No se pudo guardar el archivo: " + path;
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) {
        out_error = "Error al escribir el archivo: " + path;
        return false;
    }
    return true;
}

static int find_tab_by_path(const std::vector<OpenFileTab>& tabs, const std::string& path) {
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        if (tabs[i].path == path) return i;
    }
    return -1;
}

static std::string quote_path(const std::string& path) {
    std::string quoted = "\"";
    for (char c : path) {
        if (c == '"') quoted += '\\';
        quoted += c;
    }
    quoted += "\"";
    return quoted;
}

static int run_scara_and_capture(const std::string& source_path, std::vector<std::string>& console_lines) {
    std::string cmd = "..\\programasCOMPI\\scara.exe " + quote_path(source_path) + " 2>&1";
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        console_lines.push_back("[IDE] ERROR: no se pudo ejecutar scara.exe");
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), pipe) != nullptr) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        console_lines.push_back(s);
    }

    int status = _pclose(pipe);
    console_lines.push_back("[IDE] Proceso terminado. Codigo=" + std::to_string(status));
    return status;
}

static bool find_symbol_definition_line(const std::string& content,
                                        const std::string& symbol,
                                        int& out_line,
                                        std::string& out_kind) {
    if (symbol.empty()) return false;

    std::string sym = to_upper_copy(symbol);
    std::stringstream ss(content);
    std::string line;
    int line_no = 1;

    while (std::getline(ss, line)) {
        std::string t = trim_copy(line);
        if (starts_with(t, "#") || t.empty()) {
            line_no++;
            continue;
        }

        std::stringstream ls(t);
        std::string kw;
        std::string name;
        ls >> kw >> name;

        std::string ukw = to_upper_copy(kw);
        std::string uname = to_upper_copy(name);
        if ((ukw == "VAR" || ukw == "POINT") && uname == sym) {
            out_line = line_no;
            out_kind = ukw;
            return true;
        }

        line_no++;
    }
    return false;
}

static std::vector<SymbolItem> parse_local_symbols(const std::string& content) {
    std::vector<SymbolItem> symbols;
    std::stringstream ss(content);
    std::string line;
    int line_no = 1;

    while (std::getline(ss, line)) {
        std::string t = trim_copy(line);
        if (t.empty() || starts_with(t, "#")) {
            line_no++;
            continue;
        }

        std::stringstream ls(t);
        std::string kw;
        std::string name;
        ls >> kw >> name;

        std::string ukw = to_upper_copy(kw);
        if (ukw == "VAR" || ukw == "POINT") {
            while (!name.empty() && (name.back() == ',' || name.back() == ';' || name.back() == ':')) name.pop_back();
            if (!name.empty()) {
                SymbolItem it;
                it.kind = ukw;
                it.name = name;
                it.line = line_no;
                symbols.push_back(it);
            }
        }
        line_no++;
    }
    return symbols;
}

static bool symbol_matches_filter(const SymbolItem& s, const std::string& filter) {
    if (filter.empty()) return true;
    std::string f = to_upper_copy(filter);
    std::string k = to_upper_copy(s.kind);
    std::string n = to_upper_copy(s.name);
    return k.find(f) != std::string::npos || n.find(f) != std::string::npos;
}

static bool find_symbol_info(const std::vector<SymbolItem>& symbols,
                             const std::string& name,
                             SymbolItem& out_symbol) {
    if (name.empty()) return false;
    std::string n = to_upper_copy(name);
    for (const SymbolItem& s : symbols) {
        if (to_upper_copy(s.name) == n) {
            out_symbol = s;
            return true;
        }
    }
    return false;
}

static std::string detect_diag_kind(const std::string& line) {
    std::string u = to_upper_copy(line);
    if (u.find("ERROR") != std::string::npos) return "Error";
    if (u.find("WARN") != std::string::npos) return "Warning";
    if (u.find("INFO") != std::string::npos) return "Info";
    return "Error";
}

static bool diag_visible(const DiagnosticItem& d, const DiagnosticsFilterState& f) {
    if (d.kind == "Error") return f.show_errors;
    if (d.kind == "Warning") return f.show_warnings;
    if (d.kind == "Info") return f.show_info;
    return true;
}

static int diag_kind_rank(const DiagnosticItem& d) {
    if (d.kind == "Error") return 0;
    if (d.kind == "Warning") return 1;
    if (d.kind == "Info") return 2;
    return 3;
}

static bool diag_matches_search(const DiagnosticItem& d, const std::string& query) {
    if (query.empty()) return true;
    std::string q = to_upper_copy(query);
    std::string f = to_upper_copy(file_name_from_path(d.file_path));
    std::string k = to_upper_copy(d.kind);
    std::string m = to_upper_copy(d.message);
    return f.find(q) != std::string::npos ||
           k.find(q) != std::string::npos ||
           m.find(q) != std::string::npos ||
           std::to_string(d.line).find(q) != std::string::npos;
}

static bool diag_less(const DiagnosticItem& a, const DiagnosticItem& b, int sort_mode) {
    if (sort_mode == 1) {
        int ra = diag_kind_rank(a);
        int rb = diag_kind_rank(b);
        if (ra != rb) return ra < rb;
        if (a.file_path != b.file_path) return a.file_path < b.file_path;
        return a.line < b.line;
    }

    if (sort_mode == 2) {
        if (a.message != b.message) return a.message < b.message;
        if (a.file_path != b.file_path) return a.file_path < b.file_path;
        return a.line < b.line;
    }

    if (a.file_path != b.file_path) return a.file_path < b.file_path;
    if (a.line != b.line) return a.line < b.line;
    return a.message < b.message;
}

static bool parse_bool_value(const std::string& s, bool default_value) {
    std::string t = to_upper_copy(trim_copy(s));
    if (t == "1" || t == "TRUE" || t == "YES") return true;
    if (t == "0" || t == "FALSE" || t == "NO") return false;
    return default_value;
}

static std::filesystem::path ide_ui_state_path() {
    char exe_path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return std::filesystem::current_path() / "scara_ide_ui.cfg";
    }
    std::filesystem::path p(exe_path);
    return p.parent_path() / "scara_ide_ui.cfg";
}

static void load_ide_ui_state(bool& show_projects,
                              bool& show_console,
                              bool& show_visual_panel,
                              bool& show_quick_view,
                              std::string& explorer_search,
                              bool& filter_scara,
                              bool& filter_txt,
                              bool& filter_code,
                              bool& filter_md,
                              bool& show_diagnostics,
                              DiagnosticsFilterState& diagnostics_filter,
                              ProblemsPanelState& problems_ui,
                              EditorUiState& editor_ui) {
    std::ifstream in(ide_ui_state_path(), std::ios::binary);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim_copy(line.substr(0, eq));
        std::string v = line.substr(eq + 1);

        if (k == "show_projects") show_projects = parse_bool_value(v, show_projects);
        else if (k == "show_console") show_console = parse_bool_value(v, show_console);
        else if (k == "show_visual_panel") show_visual_panel = parse_bool_value(v, show_visual_panel);
        else if (k == "show_quick_view") show_quick_view = parse_bool_value(v, show_quick_view);
        else if (k == "explorer_search") explorer_search = v;
        else if (k == "filter_scara") filter_scara = parse_bool_value(v, filter_scara);
        else if (k == "filter_txt") filter_txt = parse_bool_value(v, filter_txt);
        else if (k == "filter_code") filter_code = parse_bool_value(v, filter_code);
        else if (k == "filter_md") filter_md = parse_bool_value(v, filter_md);
        else if (k == "show_diagnostics") show_diagnostics = parse_bool_value(v, show_diagnostics);
        else if (k == "diag_show_errors") diagnostics_filter.show_errors = parse_bool_value(v, diagnostics_filter.show_errors);
        else if (k == "diag_show_warnings") diagnostics_filter.show_warnings = parse_bool_value(v, diagnostics_filter.show_warnings);
        else if (k == "diag_show_info") diagnostics_filter.show_info = parse_bool_value(v, diagnostics_filter.show_info);
        else if (k == "problems_search") problems_ui.search_query = v;
        else if (k == "problems_sort_mode") problems_ui.sort_mode = std::atoi(v.c_str());
        else if (k == "show_symbols") editor_ui.show_symbols = parse_bool_value(v, editor_ui.show_symbols);
        else if (k == "symbol_filter") editor_ui.symbol_filter = v;
        else if (k == "show_symbol_info") editor_ui.show_symbol_info = parse_bool_value(v, editor_ui.show_symbol_info);
        else if (k == "show_editor_syntax") editor_ui.show_editor_syntax = parse_bool_value(v, editor_ui.show_editor_syntax);
        else if (k == "editor_syntax_split") editor_ui.editor_syntax_split = static_cast<float>(std::atof(v.c_str()));
    }

    if (problems_ui.sort_mode < 0 || problems_ui.sort_mode > 2) {
        problems_ui.sort_mode = 0;
    }
    editor_ui.editor_syntax_split = std::clamp(editor_ui.editor_syntax_split, 0.25f, 0.75f);
}

static void save_ide_ui_state(bool show_projects,
                              bool show_console,
                              bool show_visual_panel,
                              bool show_quick_view,
                              const std::string& explorer_search,
                              bool filter_scara,
                              bool filter_txt,
                              bool filter_code,
                              bool filter_md,
                              bool show_diagnostics,
                              const DiagnosticsFilterState& diagnostics_filter,
                              const ProblemsPanelState& problems_ui,
                              const EditorUiState& editor_ui) {
    std::ofstream out(ide_ui_state_path(), std::ios::binary | std::ios::trunc);
    if (!out) return;

    out << "show_projects=" << (show_projects ? 1 : 0) << "\n";
    out << "show_console=" << (show_console ? 1 : 0) << "\n";
    out << "show_visual_panel=" << (show_visual_panel ? 1 : 0) << "\n";
    out << "show_quick_view=" << (show_quick_view ? 1 : 0) << "\n";
    out << "explorer_search=" << explorer_search << "\n";
    out << "filter_scara=" << (filter_scara ? 1 : 0) << "\n";
    out << "filter_txt=" << (filter_txt ? 1 : 0) << "\n";
    out << "filter_code=" << (filter_code ? 1 : 0) << "\n";
    out << "filter_md=" << (filter_md ? 1 : 0) << "\n";
    out << "show_diagnostics=" << (show_diagnostics ? 1 : 0) << "\n";
    out << "diag_show_errors=" << (diagnostics_filter.show_errors ? 1 : 0) << "\n";
    out << "diag_show_warnings=" << (diagnostics_filter.show_warnings ? 1 : 0) << "\n";
    out << "diag_show_info=" << (diagnostics_filter.show_info ? 1 : 0) << "\n";
    out << "problems_search=" << problems_ui.search_query << "\n";
    out << "problems_sort_mode=" << problems_ui.sort_mode << "\n";
    out << "show_symbols=" << (editor_ui.show_symbols ? 1 : 0) << "\n";
    out << "symbol_filter=" << editor_ui.symbol_filter << "\n";
    out << "show_symbol_info=" << (editor_ui.show_symbol_info ? 1 : 0) << "\n";
    out << "show_editor_syntax=" << (editor_ui.show_editor_syntax ? 1 : 0) << "\n";
    out << "editor_syntax_split=" << editor_ui.editor_syntax_split << "\n";
}

static void parse_diagnostics_from_console(const std::string& source_path,
                                           const std::vector<std::string>& console_lines,
                                           std::vector<DiagnosticItem>& out_diags) {
    out_diags.clear();
    std::regex line_rx("linea\\s+([0-9]+)", std::regex::icase);
    std::string last_error = "";

    for (const std::string& line : console_lines) {
        if (line.find("ERROR") != std::string::npos || line.find("VM ERROR") != std::string::npos) {
            last_error = line;
        }

        std::smatch m;
        if (std::regex_search(line, m, line_rx)) {
            DiagnosticItem d;
            d.file_path = source_path;
            d.line = std::stoi(m[1].str());
            d.kind = detect_diag_kind(last_error.empty() ? line : last_error);
            d.message = last_error.empty() ? line : last_error;
            out_diags.push_back(d);
        }
    }
}

static void execute_active_tab(std::vector<OpenFileTab>& tabs,
                               int active_tab,
                               std::vector<std::string>& console_lines,
                               std::vector<DiagnosticItem>& diagnostics) {
    if (active_tab < 0 || active_tab >= static_cast<int>(tabs.size()) || !tabs[active_tab].open) {
        console_lines.push_back("[IDE] ERROR: no hay archivo activo para ejecutar.");
        return;
    }

    console_lines.push_back("[IDE] Ejecutando: " + tabs[active_tab].path);
    run_scara_and_capture(tabs[active_tab].path, console_lines);
    parse_diagnostics_from_console(tabs[active_tab].path, console_lines, diagnostics);
}

static bool open_file_dialog_native(std::string& out_path) {
    char file_buf[MAX_PATH] = {0};

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "SCARA (*.scara)\0*.scara\0Todos (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        out_path = normalize_path(file_buf);
        return true;
    }
    return false;
}

static bool open_or_activate_tab(const std::string& raw_path,
                                 std::vector<OpenFileTab>& tabs,
                                 int& active_tab,
                                 std::string& breadcrumb,
                                 std::vector<std::string>& console_lines) {
    std::string path = normalize_path(raw_path);
    std::string content;
    std::string err;

    if (!read_text_file(path, content, err)) {
        console_lines.push_back("[IDE] ERROR: " + err);
        return false;
    }

    int existing = find_tab_by_path(tabs, path);
    if (existing >= 0) {
        tabs[existing].open = true;
        active_tab = existing;
        breadcrumb = breadcrumb_from_path(path);
        console_lines.push_back("[IDE] Archivo activo: " + path);
        return true;
    }

    tabs.push_back({ path, file_name_from_path(path), content, true });
    tabs.back().syntax_cache_dirty = true;
    active_tab = static_cast<int>(tabs.size()) - 1;
    breadcrumb = breadcrumb_from_path(path);
    console_lines.push_back("[IDE] Archivo abierto: " + path);
    return true;
}

static bool save_active_tab(std::vector<OpenFileTab>& tabs,
                            int active_tab,
                            std::vector<std::string>& console_lines) {
    if (active_tab < 0 || active_tab >= static_cast<int>(tabs.size()) || !tabs[active_tab].open) {
        console_lines.push_back("[IDE] ERROR: no hay archivo activo para guardar.");
        return false;
    }

    std::string err;
    if (!write_text_file(tabs[active_tab].path, tabs[active_tab].content, err)) {
        console_lines.push_back("[IDE] ERROR: " + err);
        return false;
    }

    console_lines.push_back("[IDE] Archivo guardado: " + tabs[active_tab].path);
    return true;
}

static bool is_scara_file(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".scara" || ext == ".txt" || ext == ".c" || ext == ".h" || ext == ".md";
}

static bool path_matches_filter(const std::filesystem::path& p,
                                const std::string& search_filter,
                                bool show_scara,
                                bool show_text,
                                bool show_code,
                                bool show_markdown) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool ext_ok = false;
    if (show_scara && ext == ".scara") ext_ok = true;
    if (show_text && ext == ".txt") ext_ok = true;
    if (show_code && (ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp")) ext_ok = true;
    if (show_markdown && ext == ".md") ext_ok = true;
    if (!ext_ok) return false;

    if (search_filter.empty()) return true;
    std::string name = p.filename().string();
    std::string n1 = name;
    std::string n2 = search_filter;
    std::transform(n1.begin(), n1.end(), n1.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(n2.begin(), n2.end(), n2.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return n1.find(n2) != std::string::npos;
}

static void draw_project_tree(const std::filesystem::path& root,
                              std::vector<OpenFileTab>& tabs,
                              int& active_tab,
                              std::string& breadcrumb,
                              std::vector<std::string>& console_lines,
                              const std::string& search_filter,
                              bool show_scara,
                              bool show_text,
                              bool show_code,
                              bool show_markdown,
                              int depth = 0) {
    if (depth > 6) return;

    std::error_code ec;
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!ec) entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory()) return a.is_directory() > b.is_directory();
        return a.path().filename().string() < b.path().filename().string();
    });

    for (const auto& entry : entries) {
        const std::string label = entry.path().filename().string();
        if (entry.is_directory()) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
            bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
            if (opened) {
                draw_project_tree(entry.path(), tabs, active_tab, breadcrumb, console_lines,
                                  search_filter, show_scara, show_text, show_code, show_markdown,
                                  depth + 1);
                ImGui::TreePop();
            }
        } else if (entry.is_regular_file() && is_scara_file(entry.path()) &&
                   path_matches_filter(entry.path(), search_filter, show_scara, show_text, show_code, show_markdown)) {
            bool selected = false;
            std::string full = normalize_path(entry.path().string());
            int tab_idx = find_tab_by_path(tabs, full);
            bool dirty = (tab_idx >= 0 && tabs[tab_idx].dirty);
            std::string shown_label = label + (dirty ? " *" : "");
            if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                selected = full == tabs[active_tab].path;
            }
            if (ImGui::Selectable(shown_label.c_str(), selected)) {
                open_or_activate_tab(entry.path().string(), tabs, active_tab, breadcrumb, console_lines);
            }
        }
    }
}

static void draw_scara_quick_view(OpenFileTab& tab) {
    rebuild_syntax_cache(tab);
    draw_scara_highlighted_view("ScaraQuickView", tab.syntax_lines, tab.focus_line, -1, -1, 120, true);
}

static void apply_theme_light_custom() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.TabRounding = 4.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.95f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.90f, 0.90f, 0.88f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.21f, 0.48f, 0.79f, 0.38f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.21f, 0.48f, 0.79f, 0.60f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.21f, 0.48f, 0.79f, 0.78f);
    colors[ImGuiCol_Button] = ImVec4(0.93f, 0.93f, 0.92f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.84f, 0.90f, 0.98f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.75f, 0.84f, 0.96f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.86f, 0.86f, 0.84f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.72f, 0.84f, 0.96f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.62f, 0.78f, 0.95f, 1.00f);
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "SCARA IDE (SDL2 + ImGui)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1400,
        900,
        SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsLight();
    apply_theme_light_custom();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;
    bool show_projects = true;
    bool show_console = true;
    bool show_visual_panel = true;
    bool show_quick_view = true;
    std::string explorer_search;
    bool filter_scara = true;
    bool filter_txt = true;
    bool filter_code = true;
    bool filter_md = true;
    bool show_diagnostics = true;
    DiagnosticsFilterState diagnostics_filter;
    ProblemsPanelState problems_ui;

    std::vector<std::string> opened_projects = {
        "scara",
        "programasSCARA"
    };
    std::filesystem::path project_root = std::filesystem::current_path().parent_path();

    std::vector<OpenFileTab> tabs;
    tabs.push_back({
        "C:/LAII/Proyecto/scara/scara/programasSCARA/correctos/c1_pickandplace.scara",
        "c1_pickandplace.scara",
        "PROGRAM c1_pickandplace\n# demo\nEND\n",
        true,
        false
    });
    tabs.back().syntax_cache_dirty = true;
    tabs.push_back({
        "C:/LAII/Proyecto/scara/scara/programasSCARA/correctos/c2_condicional.scara",
        "c2_condicional.scara",
        "PROGRAM c2_condicional\n# demo\nEND\n",
        true,
        false
    });
    tabs.back().syntax_cache_dirty = true;

    int active_tab = 0;
    std::string breadcrumb = "C: > LAII > Proyecto > scara > scara > programasSCARA > correctos > c1_pickandplace.scara";
    EditorUiState editor_ui;

    load_ide_ui_state(show_projects,
                      show_console,
                      show_visual_panel,
                      show_quick_view,
                      explorer_search,
                      filter_scara,
                      filter_txt,
                      filter_code,
                      filter_md,
                      show_diagnostics,
                      diagnostics_filter,
                      problems_ui,
                      editor_ui);

    std::vector<std::string> console_lines = {
        "[IDE] Consola inicializada.",
        "[IDE] Salida del compilador/VM se mostrara aqui."
    };
    std::vector<DiagnosticItem> diagnostics;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const bool ctrl_down = (io.KeyCtrl || io.KeySuper);
        const bool shift_down = io.KeyShift;
        if (ctrl_down && shift_down && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            editor_ui.show_symbols = !editor_ui.show_symbols;
        } else if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            std::string selected;
            if (open_file_dialog_native(selected)) {
                open_or_activate_tab(selected, tabs, active_tab, breadcrumb, console_lines);
            }
        }
        if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (save_active_tab(tabs, active_tab, console_lines) &&
                active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                tabs[active_tab].dirty = false;
            }
        }
        if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            editor_ui.show_find = true;
        }
        if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            editor_ui.show_goto = true;
        }
        if (shift_down && ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            editor_ui.show_refs = true;
            if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                editor_ui.refs_symbol = word_at_cursor(tabs[active_tab].content, editor_ui.cursor_pos);
                editor_ui.refs_hits = find_symbol_usages_offsets(tabs[active_tab].content, editor_ui.refs_symbol);
                editor_ui.refs_index = editor_ui.refs_hits.empty() ? -1 : 0;
                if (editor_ui.refs_index >= 0) {
                    set_tab_focus_line(tabs[active_tab], line_from_offset(tabs[active_tab].content, editor_ui.refs_hits[editor_ui.refs_index]));
                }
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            editor_ui.show_goto_def = true;
            if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                editor_ui.goto_def_symbol = word_at_cursor(tabs[active_tab].content, editor_ui.cursor_pos);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            execute_active_tab(tabs, active_tab, console_lines, diagnostics);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
            if (!editor_ui.find_hits.empty()) {
                editor_ui.find_index = (editor_ui.find_index + 1) % static_cast<int>(editor_ui.find_hits.size());
                if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                    set_tab_focus_line(tabs[active_tab], line_from_offset(tabs[active_tab].content, editor_ui.find_hits[editor_ui.find_index]));
                }
            }
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoNavFocus |
                                      ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::Begin("SCARA_IDE_HOST", nullptr, host_flags);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Archivo")) {
                if (ImGui::MenuItem("Abrir", "Ctrl+O", false, true)) {
                    std::string selected;
                    if (open_file_dialog_native(selected)) {
                        open_or_activate_tab(selected, tabs, active_tab, breadcrumb, console_lines);
                    }
                }
                if (ImGui::MenuItem("Guardar", "Ctrl+S", false, true)) {
                    if (save_active_tab(tabs, active_tab, console_lines) &&
                        active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                        tabs[active_tab].dirty = false;
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Salir")) {
                    running = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tema")) {
                if (ImGui::MenuItem("Claro SCARA")) {
                    ImGui::StyleColorsLight();
                    apply_theme_light_custom();
                }
                ImGui::MenuItem("Editor oscuro", nullptr, false, false);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Fuente")) {
                ImGui::MenuItem("Tamano 14", nullptr, false, false);
                ImGui::MenuItem("Tamano 16", nullptr, false, false);
                ImGui::MenuItem("Selector de fuente (proximo)", nullptr, false, false);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Editar")) {
                if (ImGui::MenuItem("Buscar", "Ctrl+F", false, true)) {
                    editor_ui.show_find = true;
                }
                if (ImGui::MenuItem("Ir a linea", "Ctrl+G", false, true)) {
                    editor_ui.show_goto = true;
                }
                if (ImGui::MenuItem("Ir a definicion", "F12", false, active_tab >= 0 && active_tab < static_cast<int>(tabs.size()))) {
                    editor_ui.show_goto_def = true;
                    if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                        editor_ui.goto_def_symbol = word_at_cursor(tabs[active_tab].content, editor_ui.cursor_pos);
                    }
                }
                if (ImGui::MenuItem("Buscar referencias", "Shift+F12", false, active_tab >= 0 && active_tab < static_cast<int>(tabs.size()))) {
                    editor_ui.show_refs = true;
                    if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                        editor_ui.refs_symbol = word_at_cursor(tabs[active_tab].content, editor_ui.cursor_pos);
                        editor_ui.refs_hits = find_symbol_usages_offsets(tabs[active_tab].content, editor_ui.refs_symbol);
                        editor_ui.refs_index = editor_ui.refs_hits.empty() ? -1 : 0;
                        if (editor_ui.refs_index >= 0) {
                            set_tab_focus_line(tabs[active_tab], line_from_offset(tabs[active_tab].content, editor_ui.refs_hits[editor_ui.refs_index]));
                        }
                    }
                }
                if (ImGui::MenuItem("Panel de simbolos", "Ctrl+Shift+O", editor_ui.show_symbols, true)) {
                    editor_ui.show_symbols = !editor_ui.show_symbols;
                }
                if (ImGui::MenuItem("Info de simbolo en cursor", nullptr, editor_ui.show_symbol_info, true)) {
                    editor_ui.show_symbol_info = !editor_ui.show_symbol_info;
                }
                if (ImGui::MenuItem("Resaltado sintactico (editor)", nullptr, editor_ui.show_editor_syntax, true)) {
                    editor_ui.show_editor_syntax = !editor_ui.show_editor_syntax;
                }
                if (ImGui::MenuItem("Siguiente coincidencia", "F3", false, !editor_ui.find_hits.empty())) {
                    if (!editor_ui.find_hits.empty()) {
                        editor_ui.find_index = (editor_ui.find_index + 1) % static_cast<int>(editor_ui.find_hits.size());
                        if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                            set_tab_focus_line(tabs[active_tab], line_from_offset(tabs[active_tab].content, editor_ui.find_hits[editor_ui.find_index]));
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Ayuda")) {
                ImGui::MenuItem("Documentacion SCARA (proximo)", nullptr, false, false);
                ImGui::MenuItem("Atajos de teclado (proximo)", nullptr, false, false);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Ejecutar archivo", "F5", false, active_tab >= 0 && active_tab < static_cast<int>(tabs.size()) && tabs[active_tab].open)) {
                execute_active_tab(tabs, active_tab, console_lines, diagnostics);
            }
            ImGui::EndMenuBar();
        }

        ImGui::BeginChild("BreadcrumbBar", ImVec2(0, 34), true);
        ImGui::TextUnformatted(breadcrumb.c_str());
        ImGui::EndChild();

        float console_height = show_console ? 190.0f : 0.0f;
        float body_height = ImGui::GetContentRegionAvail().y - console_height;
        if (body_height < 120.0f) body_height = 120.0f;

        ImGui::BeginChild("MainBody", ImVec2(0, body_height), false);

        if (show_projects) {
            ImGui::BeginChild("ProjectsPanel", ImVec2(260, 0), true);
            ImGui::TextUnformatted("Proyectos Abiertos");
            ImGui::Separator();
            for (const std::string& p : opened_projects) {
                ImGui::BulletText("%s", p.c_str());
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Explorador");
            ImGui::Separator();
            ImGui::InputTextWithHint("##search", "Buscar archivo...", &explorer_search);
            ImGui::Checkbox(".scara", &filter_scara);
            ImGui::SameLine();
            ImGui::Checkbox(".txt", &filter_txt);
            ImGui::SameLine();
            ImGui::Checkbox(".c/.h/.cpp", &filter_code);
            ImGui::SameLine();
            ImGui::Checkbox(".md", &filter_md);
            ImGui::Separator();
            draw_project_tree(project_root, tabs, active_tab, breadcrumb, console_lines,
                              explorer_search, filter_scara, filter_txt, filter_code, filter_md);

            if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size()) && tabs[active_tab].open) {
                ImGui::Separator();
                ImGui::Text("Activo: %s", tabs[active_tab].name.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(tabs[active_tab].dirty ? "*" : "");
            }
            ImGui::Separator();
            if (ImGui::Button("Ocultar panel")) {
                show_projects = false;
            }
            ImGui::EndChild();
            ImGui::SameLine();
        } else {
            if (ImGui::Button(">> Proyectos")) {
                show_projects = true;
            }
            ImGui::SameLine();
        }

        ImGui::BeginChild("EditorAndViz", ImVec2(0, 0), false);

        if (ImGui::BeginTabBar("FileTabs")) {
            for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
                if (!tabs[i].open) continue;

                std::string tab_label = tabs[i].name + (tabs[i].dirty ? " *" : "");
                ImGuiTabItemFlags tab_flags = (i == active_tab) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(tab_label.c_str(), &tabs[i].open, tab_flags)) {
                    active_tab = i;
                    breadcrumb = breadcrumb_from_path(tabs[i].path);
                    rebuild_syntax_cache(tabs[i]);

                    ImGui::BeginChild("EditorRegion", ImVec2(0, show_visual_panel ? 360 : 0), true);
                    ImGuiInputTextFlags editor_flags = ImGuiInputTextFlags_AllowTabInput;
                    editor_flags |= ImGuiInputTextFlags_CallbackAlways;
                    CursorCaptureData cap{ &editor_ui.cursor_pos };

                    if (editor_ui.show_editor_syntax) {
                        int focus_ln = tabs[i].focus_line > 0 ? tabs[i].focus_line : editor_ui.line;
                        ImGui::TextUnformatted("Editor principal (F1): resaltado + edicion");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(180.0f);
                        if (ImGui::SliderFloat("##syntax_split", &editor_ui.editor_syntax_split, 0.25f, 0.75f, "Split %.0f%%")) {
                            editor_ui.editor_syntax_split = std::clamp(editor_ui.editor_syntax_split, 0.25f, 0.75f);
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("Salto=rojo, Cursor=azul");
                        ImGui::Separator();
                        float left_weight = std::clamp(editor_ui.editor_syntax_split, 0.25f, 0.75f);
                        float right_weight = 1.0f - left_weight;
                        if (ImGui::BeginTable("EditorMainSplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                            ImGui::TableSetupColumn("Resaltado", ImGuiTableColumnFlags_WidthStretch, left_weight);
                            ImGui::TableSetupColumn("Edicion", ImGuiTableColumnFlags_WidthStretch, right_weight);
                            ImGui::TableNextColumn();
                            draw_scara_highlighted_view("EditorSyntaxMainInner", tabs[i].syntax_lines, focus_ln, editor_ui.line, tabs[i].syntax_scroll_line, 0, false);
                            tabs[i].syntax_scroll_line = -1;

                            ImGui::TableNextColumn();
                            if (ImGui::InputTextMultiline("##editor", &tabs[i].content, ImVec2(-1, -1), editor_flags,
                                                          imgui_capture_cursor_cb, &cap)) {
                                tabs[i].dirty = true;
                                tabs[i].syntax_cache_dirty = true;
                            }
                            ImGui::EndTable();
                        }
                    } else {
                        if (ImGui::InputTextMultiline("##editor", &tabs[i].content, ImVec2(-1, -1), editor_flags,
                                                      imgui_capture_cursor_cb, &cap)) {
                            tabs[i].dirty = true;
                            tabs[i].syntax_cache_dirty = true;
                        }
                    }
                    compute_line_col(tabs[i].content, editor_ui.cursor_pos, editor_ui.line, editor_ui.col);
                    ImGui::EndChild();

                    std::vector<SymbolItem> symbols = parse_local_symbols(tabs[i].content);
                    if (editor_ui.show_symbol_info) {
                        std::string cursor_word = word_at_cursor(tabs[i].content, editor_ui.cursor_pos);
                        SymbolItem sym;
                        bool has_sym = find_symbol_info(symbols, cursor_word, sym);
                        if (has_sym) {
                            int uses = static_cast<int>(find_symbol_usages_offsets(tabs[i].content, sym.name).size());
                            ImGui::BeginChild("SymbolInfoBar", ImVec2(0, 34), true);
                            ImGui::Text("Simbolo: %s  |  Tipo: %s  |  Def: L%d  |  Usos: %d",
                                        sym.name.c_str(), sym.kind.c_str(), sym.line, uses);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Ir##symbol_info")) {
                                set_tab_focus_line(tabs[i], sym.line);
                            }
                            ImGui::EndChild();
                        }
                    }

                    if (editor_ui.show_symbols) {
                        ImGui::BeginChild("SymbolsPanel", ImVec2(0, 110), true);
                        ImGui::TextUnformatted("Simbolos locales");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::InputTextWithHint("##symbols_filter", "Filtrar simbolos...", &editor_ui.symbol_filter);
                        ImGui::SameLine();
                        ImGui::Text("%d", static_cast<int>(symbols.size()));
                        ImGui::Separator();

                        for (const SymbolItem& s : symbols) {
                            if (!symbol_matches_filter(s, editor_ui.symbol_filter)) continue;

                            if (s.kind == "VAR") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.16f, 0.44f, 0.80f, 1.0f));
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.60f, 0.25f, 1.0f));
                            }
                            std::string label = s.kind + " " + s.name + "  (L" + std::to_string(s.line) + ")";
                            if (ImGui::Selectable(label.c_str(), false)) {
                                set_tab_focus_line(tabs[i], s.line);
                                console_lines.push_back("[IDE] Simbolo " + s.kind + " '" + s.name + "' en linea " + std::to_string(s.line));
                            }
                            ImGui::PopStyleColor();
                        }
                        ImGui::EndChild();
                    }

                    if (editor_ui.show_find) {
                        ImGui::BeginChild("FindBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Buscar:");
                        ImGui::SameLine();
                        bool changed = ImGui::InputText("##find_query", &editor_ui.find_query);
                        if (changed) {
                            editor_ui.find_hits = find_all_hits(tabs[i].content, editor_ui.find_query);
                            editor_ui.find_index = editor_ui.find_hits.empty() ? -1 : 0;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Prev") && !editor_ui.find_hits.empty()) {
                            editor_ui.find_index = (editor_ui.find_index - 1 + static_cast<int>(editor_ui.find_hits.size())) % static_cast<int>(editor_ui.find_hits.size());
                            set_tab_focus_line(tabs[i], line_from_offset(tabs[i].content, editor_ui.find_hits[editor_ui.find_index]));
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Next") && !editor_ui.find_hits.empty()) {
                            editor_ui.find_index = (editor_ui.find_index + 1) % static_cast<int>(editor_ui.find_hits.size());
                            set_tab_focus_line(tabs[i], line_from_offset(tabs[i].content, editor_ui.find_hits[editor_ui.find_index]));
                        }
                        ImGui::SameLine();
                        ImGui::Text("%d coincidencias", static_cast<int>(editor_ui.find_hits.size()));
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar")) {
                            editor_ui.show_find = false;
                        }
                        ImGui::EndChild();
                    }

                    if (editor_ui.show_goto) {
                        ImGui::BeginChild("GotoBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Ir a linea:");
                        ImGui::SameLine();
                        ImGui::InputText("##goto_line", &editor_ui.goto_line_input);
                        ImGui::SameLine();
                        if (ImGui::Button("Ir")) {
                            int ln = std::atoi(editor_ui.goto_line_input.c_str());
                            if (ln > 0) {
                                set_tab_focus_line(tabs[i], ln);
                                console_lines.push_back("[IDE] Foco en linea " + std::to_string(ln));
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar##goto")) {
                            editor_ui.show_goto = false;
                        }
                        ImGui::EndChild();
                    }

                    if (editor_ui.show_goto_def) {
                        ImGui::BeginChild("GotoDefBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Ir a definicion:");
                        ImGui::SameLine();
                        ImGui::InputText("##goto_def_symbol", &editor_ui.goto_def_symbol);
                        ImGui::SameLine();
                        if (ImGui::Button("Buscar##goto_def")) {
                            int def_line = -1;
                            std::string def_kind;
                            if (find_symbol_definition_line(tabs[i].content, editor_ui.goto_def_symbol, def_line, def_kind)) {
                                set_tab_focus_line(tabs[i], def_line);
                                console_lines.push_back("[IDE] Definicion " + def_kind + " de '" + editor_ui.goto_def_symbol + "' en linea " + std::to_string(def_line));
                            } else {
                                console_lines.push_back("[IDE] No se encontro definicion local para: " + editor_ui.goto_def_symbol);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar##goto_def")) {
                            editor_ui.show_goto_def = false;
                        }
                        ImGui::EndChild();
                    }

                    if (editor_ui.show_refs) {
                        ImGui::BeginChild("RefsBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Referencias:");
                        ImGui::SameLine();
                        bool changed_refs = ImGui::InputText("##refs_symbol", &editor_ui.refs_symbol);
                        ImGui::SameLine();
                        if (ImGui::Button("Buscar##refs") || changed_refs) {
                            editor_ui.refs_hits = find_symbol_usages_offsets(tabs[i].content, editor_ui.refs_symbol);
                            editor_ui.refs_index = editor_ui.refs_hits.empty() ? -1 : 0;
                            if (editor_ui.refs_index >= 0) {
                                set_tab_focus_line(tabs[i], line_from_offset(tabs[i].content, editor_ui.refs_hits[editor_ui.refs_index]));
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Prev##refs") && !editor_ui.refs_hits.empty()) {
                            editor_ui.refs_index = (editor_ui.refs_index - 1 + static_cast<int>(editor_ui.refs_hits.size())) % static_cast<int>(editor_ui.refs_hits.size());
                            set_tab_focus_line(tabs[i], line_from_offset(tabs[i].content, editor_ui.refs_hits[editor_ui.refs_index]));
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Next##refs") && !editor_ui.refs_hits.empty()) {
                            editor_ui.refs_index = (editor_ui.refs_index + 1) % static_cast<int>(editor_ui.refs_hits.size());
                            set_tab_focus_line(tabs[i], line_from_offset(tabs[i].content, editor_ui.refs_hits[editor_ui.refs_index]));
                        }
                        ImGui::SameLine();
                        ImGui::Text("%d refs", static_cast<int>(editor_ui.refs_hits.size()));
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar##refs")) {
                            editor_ui.show_refs = false;
                        }
                        ImGui::EndChild();
                    }

                    {
                        static const char* kwords[] = {
                            "PROGRAM", "END", "VAR", "POINT", "MOVE", "MOVEJ", "APPROACH", "DEPART",
                            "HOME", "OPEN", "CLOSE", "SPEED", "WAIT", "IF", "ELSE", "END_IF",
                            "WHILE", "END_WHILE", "REPEAT", "END_REPEAT", "PRINT", "HALT"
                        };
                        int pref_len = 0;
                        std::string pref = to_upper_copy(current_word_prefix(tabs[i].content, editor_ui.cursor_pos, pref_len));
                        std::vector<std::string> sugg;
                        if (!pref.empty()) {
                            for (const char* kw : kwords) {
                                std::string skw = kw;
                                if (starts_with(skw, pref) && skw != pref) {
                                    sugg.push_back(skw);
                                }
                            }
                        }
                        if (!sugg.empty()) {
                            ImGui::BeginChild("AutocompleteBar", ImVec2(0, 48), true);
                            ImGui::TextUnformatted("Sugerencias:");
                            ImGui::SameLine();
                            for (int s = 0; s < static_cast<int>(sugg.size()) && s < 8; ++s) {
                                if (ImGui::SmallButton(sugg[s].c_str())) {
                                    insert_completion_at_cursor(tabs[i].content, editor_ui.cursor_pos, sugg[s], pref_len);
                                    tabs[i].dirty = true;
                                }
                                ImGui::SameLine();
                            }
                            ImGui::NewLine();
                            ImGui::EndChild();
                        }
                    }

                    ImGui::Checkbox("Vista rapida SCARA", &show_quick_view);
                    ImGui::SameLine();
                    if (ImGui::Button("Guardar archivo")) {
                        if (save_active_tab(tabs, i, console_lines)) {
                            tabs[i].dirty = false;
                        }
                    }
                    if (show_quick_view) {
                        draw_scara_quick_view(tabs[i]);
                    }

                    ImGui::BeginChild("StatusBar", ImVec2(0, 24), true);
                    ImGui::Text("%s | Ln %d, Col %d | %s",
                                tabs[i].name.c_str(),
                                editor_ui.line,
                                editor_ui.col,
                                tabs[i].dirty ? "Sin guardar" : "Guardado");
                    ImGui::EndChild();

                    if (show_visual_panel) {
                        ImGui::BeginChild("VisualizationPanel", ImVec2(0, 0), true);
                        ImGui::TextUnformatted("Visualizacion Isometrica Integrada (MVP)");
                        ImGui::Separator();
                        ImGui::TextUnformatted("Aqui se integrara la salida SDL del robot en un panel acoplable.");
                        ImGui::TextUnformatted("Opcion adicional: abrir visualizacion en ventana externa.");
                        ImGui::Spacing();
                        if (ImGui::Button("Ejecutar archivo seleccionado")) {
                            active_tab = i;
                            execute_active_tab(tabs, active_tab, console_lines, diagnostics);
                        }
                        ImGui::SameLine();
                        ImGui::Checkbox("Panel visual activo", &show_visual_panel);
                        ImGui::EndChild();
                    }

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::EndChild();
        ImGui::EndChild();

        if (show_console) {
            ImGui::BeginChild("ConsolePanel", ImVec2(0, 0), true);
            ImGui::TextUnformatted("Consola");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 140);
            if (ImGui::Button("Limpiar")) {
                console_lines.clear();
                diagnostics.clear();
            }
            ImGui::SameLine();
            ImGui::Checkbox("Mostrar", &show_console);
            ImGui::Separator();

            ImGui::Checkbox("Problemas", &show_diagnostics);
            if (show_diagnostics) {
                int count_error = 0;
                int count_warn = 0;
                int count_info = 0;
                std::vector<DiagnosticItem> shown_diags;
                for (const DiagnosticItem& it : diagnostics) {
                    if (it.kind == "Error") count_error++;
                    else if (it.kind == "Warning") count_warn++;
                    else if (it.kind == "Info") count_info++;
                    if (!diag_visible(it, diagnostics_filter)) continue;
                    if (!diag_matches_search(it, problems_ui.search_query)) continue;
                    shown_diags.push_back(it);
                }

                std::sort(shown_diags.begin(), shown_diags.end(), [&](const DiagnosticItem& a, const DiagnosticItem& b) {
                    return diag_less(a, b, problems_ui.sort_mode);
                });

                ImGui::SameLine();
                ImGui::Text("Items: %d/%d", static_cast<int>(shown_diags.size()), static_cast<int>(diagnostics.size()));
                ImGui::SameLine();
                ImGui::Checkbox("E", &diagnostics_filter.show_errors);
                ImGui::SameLine();
                ImGui::Text("%d", count_error);
                ImGui::SameLine();
                ImGui::Checkbox("W", &diagnostics_filter.show_warnings);
                ImGui::SameLine();
                ImGui::Text("%d", count_warn);
                ImGui::SameLine();
                ImGui::Checkbox("I", &diagnostics_filter.show_info);
                ImGui::SameLine();
                ImGui::Text("%d", count_info);
                ImGui::SetNextItemWidth(240.0f);
                ImGui::InputTextWithHint("##problems_search", "Filtrar problemas...", &problems_ui.search_query);
                ImGui::SameLine();
                const char* sort_items[] = { "Archivo/Linea", "Severidad", "Mensaje" };
                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo("##problems_sort", &problems_ui.sort_mode, sort_items, IM_ARRAYSIZE(sort_items));
                ImGui::BeginChild("DiagnosticsPanel", ImVec2(0, 110), true);
                for (int d = 0; d < static_cast<int>(shown_diags.size()); ++d) {
                    const DiagnosticItem& it = shown_diags[d];
                    std::string label = "[" + it.kind + "] " + file_name_from_path(it.file_path) + ":" + std::to_string(it.line) + "  " + it.message;
                    if (it.kind == "Error") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.18f, 0.18f, 1.0f));
                    } else if (it.kind == "Warning") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.55f, 0.10f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.18f, 0.48f, 0.82f, 1.0f));
                    }
                    if (ImGui::Selectable(label.c_str(), false)) {
                        if (open_or_activate_tab(it.file_path, tabs, active_tab, breadcrumb, console_lines)) {
                            if (active_tab >= 0 && active_tab < static_cast<int>(tabs.size())) {
                                set_tab_focus_line(tabs[active_tab], it.line);
                            }
                        }
                    }
                    ImGui::PopStyleColor();
                }
                ImGui::EndChild();
            }

            ImGui::BeginChild("ConsoleLog", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (const std::string& line : console_lines) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::EndChild();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 244, 243, 240, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    save_ide_ui_state(show_projects,
                      show_console,
                      show_visual_panel,
                      show_quick_view,
                      explorer_search,
                      filter_scara,
                      filter_txt,
                      filter_code,
                      filter_md,
                      show_diagnostics,
                      diagnostics_filter,
                      problems_ui,
                      editor_ui);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
