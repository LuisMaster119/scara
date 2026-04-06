#include <SDL2/SDL.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <deque>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
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

struct TabArchivoAbierto {
    std::string ruta;
    std::string nombre;
    std::string contenido;
    bool abierta = true;
    bool modificada = false;
    int linea_foco = -1;
    int syntax_scroll_line = -1;
    std::vector<std::string> syntax_lines;
    bool syntax_cache_dirty = true;
};

struct ItemDiagnostico {
    std::string file_path;
    int line = -1;
    std::string kind;
    std::string message;
};

struct ItemSimbolo {
    std::string kind;
    std::string nombre;
    int line = -1;
};

struct EstadoUiEditor {
    int cursor_pos = 0;
    int line = 1;
    int col = 1;
    bool mostrar_editor_sintaxis = true;
    float editor_syntax_split = 0.46f;
    bool mostrar_buscar = false;
    bool mostrar_ir_a = false;
    std::string consulta_buscar;
    std::vector<int> coincidencias_buscar;
    int indice_buscar = -1;
    std::string entrada_ir_a_linea;
    bool mostrar_ir_def = false;
    std::string simbolo_ir_def;
    bool mostrar_refs = false;
    std::string simbolo_refs;
    std::vector<int> coincidencias_refs;
    int indice_refs = -1;
    std::string origen_refs = "manual";
    bool mostrar_simbolos = true;
    std::string filtro_simbolo;
    bool mostrar_info_simbolo = true;
};

struct EstadoFiltroDiagnosticos {
    bool mostrar_errores = true;
    bool mostrar_advertencias = true;
    bool mostrar_info = true;
};

struct EstadoPanelProblemas {
    std::string consulta_busqueda;
    int modo_orden = 0;  // 0=archivo/linea, 1=severidad, 2=mensaje
    int indice_seleccionado = -1;
    std::string firma_seleccionada;
};

struct DatosCapturaCursor {
    int* cursor_pos;
};

struct EstadoVisualEmbebido {
    int x = 0;
    int y = 0;
    int z = 0;
    int pinza_abierta = 1;
    int velocidad = 100;
};

struct CorridaVisualEmbebida {
    std::string source_path;
    std::vector<EstadoVisualEmbebido> timeline;
    std::vector<int> timeline_console_lines;
    int vm_position_lines = 0;
    bool saw_vm_output = false;
    int vm_output_lines = 0;
    int console_start_line = -1;
    int console_end_line = -1;
};

struct SnapshotComparacionEmbebida {
    std::string text;
    double dist = 0.0;
    int run_a = -1;
    int run_b = -1;
    int step_a = -1;
    int step_b = -1;
    bool pinned = false;
    int dx = 0;
    int dy = 0;
    int dz = 0;
    double progress_a = 0.0;
    double progress_b = 0.0;
};

struct EstadoUiCorridaEmbebida {
    int idx = 0;
    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);
    bool valido = false;
};

struct PresetFiltroSnapshotEmbebido {
    int filtro = 0;
    int orden = 0;
    float min_dist = 0.0f;
    bool limitar_max = false;
    float dist_max = 100.0f;
    bool valido = false;
};

enum class TipoEventoUi : int {
    Info,
    Error
};

static void registrar_evento_ui_operativo(std::vector<std::string>& console_lines,
                                          TipoEventoUi tipo,
                                          const char* contexto,
                                          const std::string& detalle);

static bool es_caracter_palabra(char c);
static std::string copia_mayus(std::string s);
static bool abrir_o_activar_tab(const std::string& raw_path,
                                 std::vector<TabArchivoAbierto>& tabs,
                                 int& pestana_activa,
                                 std::string& breadcrumb,
                                 std::vector<std::string>& console_lines);
static std::vector<int> buscar_offsets_usos_simbolo(const std::string& text, const std::string& simbolo);
static bool diagnostico_visible(const ItemDiagnostico& d, const EstadoFiltroDiagnosticos& f);
static bool diagnostico_coincide_busqueda(const ItemDiagnostico& d, const std::string& consulta);
static bool diagnostico_menor(const ItemDiagnostico& a, const ItemDiagnostico& b, int modo_orden);
static std::string nombre_archivo_desde_ruta(const std::string& ruta);
static int indice_timeline_desde_linea_consola(const CorridaVisualEmbebida& run, int console_line);
static int indice_corrida_desde_linea_consola(const std::vector<CorridaVisualEmbebida>& runs, int console_line);

static bool es_inicio_ident(char c) {
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool es_caracter_ident(char c) {
    return es_inicio_ident(c) || (c >= '0' && c <= '9');
}

static bool es_palabra_clave_scara_mayus(const std::string& word_upper) {
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

static bool es_inicio_numerico(const std::string& line, int pos) {
    if (pos < 0 || pos >= static_cast<int>(line.size())) return false;
    char c = line[pos];
    if (c >= '0' && c <= '9') return true;
    if ((c == '-' || c == '+') && pos + 1 < static_cast<int>(line.size())) {
        char n = line[pos + 1];
        if (n < '0' || n > '9') return false;
        if (pos == 0) return true;
        char p = line[pos - 1];
        return !es_caracter_ident(p);
    }
    return false;
}

static void dibujar_token_inline_coloreado(const std::string& token, const ImVec4& color) {
    if (token.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(token.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);
}

static void dibujar_linea_scara_resaltada(const std::string& line, int line_no, int linea_foco, int cursor_line) {
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
    if (linea_foco == line_no) {
        gutter_col = col_focus;
    } else if (cursor_line == line_no) {
        gutter_col = col_cursor;
    }
    dibujar_token_inline_coloreado(prefix, gutter_col);

    int i = 0;
    const int n = static_cast<int>(line.size());
    while (i < n) {
        char c = line[i];

        if (c == '#') {
            dibujar_token_inline_coloreado(line.substr(static_cast<size_t>(i)), col_comment);
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
            dibujar_token_inline_coloreado(line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i)), col_string);
            i = j;
            continue;
        }

        if (es_inicio_numerico(line, i)) {
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
            dibujar_token_inline_coloreado(line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i)), col_number);
            i = j;
            continue;
        }

        if (es_inicio_ident(c)) {
            int j = i + 1;
            while (j < n && es_caracter_ident(line[j])) j++;
            std::string word = line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i));
            std::string up = copia_mayus(word);
            dibujar_token_inline_coloreado(word, es_palabra_clave_scara_mayus(up) ? col_keyword : col_default);
            i = j;
            continue;
        }

        int j = i + 1;
        while (j < n && line[j] != '#' && line[j] != '"' &&
               !es_inicio_numerico(line, j) && !es_inicio_ident(line[j])) {
            j++;
        }
        dibujar_token_inline_coloreado(line.substr(static_cast<size_t>(i), static_cast<size_t>(j - i)), col_default);
        i = j;
    }

    ImGui::NewLine();
}

static void dibujar_vista_scara_resaltada(const char* child_id,
                                        const std::vector<std::string>& lines,
                                        int linea_foco,
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
            dibujar_linea_scara_resaltada(lines[i], i + 1, linea_foco, cursor_line);
        }
    }

    ImGui::EndChild();
}

static bool empieza_con(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool parsear_pos_vm_de_linea(const std::string& line, int& x, int& y, int& z) {
    size_t p = line.find("posicion (");
    if (p == std::string::npos) return false;
    int px = 0, py = 0, pz = 0;
    if (std::sscanf(line.c_str() + p, "posicion (%d,%d,%d)", &px, &py, &pz) == 3) {
        x = px;
        y = py;
        z = pz;
        return true;
    }
    return false;
}

static void reconstruir_corridas_vis_embebida(const std::vector<std::string>& console_lines,
                                      std::vector<CorridaVisualEmbebida>& out_runs) {
    out_runs.clear();

    CorridaVisualEmbebida corrida_actual;
    bool corrida_activa = false;
    int pinza_actual = 1;
    int velocidad_actual = 100;

    auto agregar_corrida_si_valida = [&](int linea_siguiente_corrida) {
        if (corrida_activa) {
            if (corrida_actual.console_start_line < 0) corrida_actual.console_start_line = 0;
            corrida_actual.console_end_line = linea_siguiente_corrida > 0 ? (linea_siguiente_corrida - 1) : linea_siguiente_corrida;
            out_runs.push_back(corrida_actual);
        }
    };

    for (int indice_linea = 0; indice_linea < static_cast<int>(console_lines.size()); ++indice_linea) {
        const std::string& line = console_lines[indice_linea];
        if (empieza_con(line, "[IDE] Ejecutando:")) {
            agregar_corrida_si_valida(indice_linea);
            corrida_actual = CorridaVisualEmbebida{};
            corrida_activa = true;
            pinza_actual = 1;
            velocidad_actual = 100;
            corrida_actual.console_start_line = indice_linea;

            std::string p = line.substr(std::string("[IDE] Ejecutando:").size());
            while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.erase(p.begin());
            corrida_actual.source_path = p;
            continue;
        }

        if (!corrida_activa) continue;

        if (line.find("[VM]") != std::string::npos) {
            corrida_actual.saw_vm_output = true;
            corrida_actual.vm_output_lines++;
        }

        if (line.find("[VM] Pinza: OPEN") != std::string::npos) {
            pinza_actual = 1;
            continue;
        }
        if (line.find("[VM] Pinza: CLOSE") != std::string::npos) {
            pinza_actual = 0;
            continue;
        }

        if (line.find("[VM] SPEED =") != std::string::npos) {
            int v = 100;
            if (std::sscanf(line.c_str(), "[VM] SPEED = %d%%", &v) == 1) {
                if (v < 1) v = 1;
                if (v > 100) v = 100;
                velocidad_actual = v;
            }
            continue;
        }

        int x = 0, y = 0, z = 0;
        if (parsear_pos_vm_de_linea(line, x, y, z)) {
            EstadoVisualEmbebido st;
            st.x = x;
            st.y = y;
            st.z = z;
            st.pinza_abierta = pinza_actual;
            st.velocidad = velocidad_actual;
            corrida_actual.timeline.push_back(st);
            corrida_actual.timeline_console_lines.push_back(indice_linea);
            corrida_actual.vm_position_lines++;
        }
    }

    agregar_corrida_si_valida(static_cast<int>(console_lines.size()));
}

static int indice_timeline_desde_linea_consola(const CorridaVisualEmbebida& run, int console_line) {
    if (run.timeline_console_lines.empty()) return -1;
    int mejor_indice = -1;
    for (int i = 0; i < static_cast<int>(run.timeline_console_lines.size()); ++i) {
        int li = run.timeline_console_lines[i];
        if (li <= console_line) {
            mejor_indice = i;
            continue;
        }
        if (mejor_indice < 0) mejor_indice = i;
        break;
    }
    if (mejor_indice < 0) mejor_indice = static_cast<int>(run.timeline_console_lines.size()) - 1;
    if (mejor_indice >= static_cast<int>(run.timeline.size())) mejor_indice = static_cast<int>(run.timeline.size()) - 1;
    return mejor_indice;
}

static int indice_corrida_desde_linea_consola(const std::vector<CorridaVisualEmbebida>& runs, int console_line) {
    for (int i = 0; i < static_cast<int>(runs.size()); ++i) {
        if (runs[i].console_start_line < 0 || runs[i].console_end_line < runs[i].console_start_line) continue;
        if (console_line >= runs[i].console_start_line && console_line <= runs[i].console_end_line) {
            return i;
        }
    }
    return -1;
}

static void dibujar_panel_visual_embebido(const std::vector<EstadoVisualEmbebido>& timeline,
                                       int indice_actual,
                                       float zoom,
                                       ImVec2& pan,
                                       bool centrar_en_actual,
                                       const std::vector<EstadoVisualEmbebido>* timeline_comparada,
                                       int indice_comparado,
                                       bool comparada_fantasma) {
    ImVec2 posicion_canvas = ImGui::GetCursorScreenPos();
    ImVec2 tamano_canvas = ImGui::GetContentRegionAvail();
    if (tamano_canvas.x < 120.0f) tamano_canvas.x = 120.0f;
    if (tamano_canvas.y < 200.0f) tamano_canvas.y = 200.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(posicion_canvas, ImVec2(posicion_canvas.x + tamano_canvas.x, posicion_canvas.y + tamano_canvas.y), IM_COL32(247, 246, 242, 255));
    dl->AddRect(posicion_canvas, ImVec2(posicion_canvas.x + tamano_canvas.x, posicion_canvas.y + tamano_canvas.y), IM_COL32(170, 170, 170, 255));

    const float cx = posicion_canvas.x + tamano_canvas.x * 0.5f + pan.x;
    const float cy = posicion_canvas.y + tamano_canvas.y * 0.68f + pan.y;

    auto map_xy = [&](int x, int y, int z) -> ImVec2 {
        float sx = static_cast<float>(x - y) * 0.45f * zoom;
        float sy = static_cast<float>(x + y) * 0.23f * zoom - static_cast<float>(z) * 0.85f * zoom;
        return ImVec2(cx + sx, cy - sy);
    };

    if (centrar_en_actual && !timeline.empty()) {
        int ci = indice_actual;
        if (ci < 0) ci = 0;
        if (ci >= static_cast<int>(timeline.size())) ci = static_cast<int>(timeline.size()) - 1;
        const EstadoVisualEmbebido& st = timeline[ci];
        float sx = static_cast<float>(st.x - st.y) * 0.45f * zoom;
        float sy = static_cast<float>(st.x + st.y) * 0.23f * zoom - static_cast<float>(st.z) * 0.85f * zoom;
        pan.x = -sx;
        pan.y = sy - tamano_canvas.y * 0.13f;
    }

    for (int g = -350; g <= 350; g += 50) {
        ImVec2 a = map_xy(g, -350, 0);
        ImVec2 b = map_xy(g, 350, 0);
        ImVec2 c = map_xy(-350, g, 0);
        ImVec2 d = map_xy(350, g, 0);
        dl->AddLine(a, b, IM_COL32(225, 225, 225, 255), 1.0f);
        dl->AddLine(c, d, IM_COL32(225, 225, 225, 255), 1.0f);
    }

    if (timeline_comparada && !timeline_comparada->empty() && indice_comparado >= 0) {
        int indice_maximo_comparado = indice_comparado;
        if (indice_maximo_comparado >= static_cast<int>(timeline_comparada->size())) indice_maximo_comparado = static_cast<int>(timeline_comparada->size()) - 1;
        ImU32 color_linea_comparada = comparada_fantasma ? IM_COL32(35, 135, 200, 120) : IM_COL32(35, 135, 200, 255);
        ImU32 color_punto_comparado = comparada_fantasma ? IM_COL32(20, 95, 170, 170) : IM_COL32(20, 95, 170, 255);

        for (int i = 1; i <= indice_maximo_comparado; ++i) {
            ImVec2 p1 = map_xy((*timeline_comparada)[i - 1].x, (*timeline_comparada)[i - 1].y, (*timeline_comparada)[i - 1].z);
            ImVec2 p2 = map_xy((*timeline_comparada)[i].x, (*timeline_comparada)[i].y, (*timeline_comparada)[i].z);
            dl->AddLine(p1, p2, color_linea_comparada, 2.0f);
        }

        const EstadoVisualEmbebido& cst = (*timeline_comparada)[indice_maximo_comparado];
        ImVec2 cp = map_xy(cst.x, cst.y, cst.z);
        dl->AddCircle(cp, 5.5f, color_punto_comparado, 0, 2.0f);
    }

    if (!timeline.empty() && indice_actual >= 0) {
        int indice_maximo_actual = indice_actual;
        if (indice_maximo_actual >= static_cast<int>(timeline.size())) indice_maximo_actual = static_cast<int>(timeline.size()) - 1;

        for (int i = 1; i <= indice_maximo_actual; ++i) {
            ImVec2 p1 = map_xy(timeline[i - 1].x, timeline[i - 1].y, timeline[i - 1].z);
            ImVec2 p2 = map_xy(timeline[i].x, timeline[i].y, timeline[i].z);
            dl->AddLine(p1, p2, IM_COL32(110, 110, 110, 255), 2.0f);
        }

        const EstadoVisualEmbebido& st = timeline[indice_maximo_actual];
        ImVec2 p = map_xy(st.x, st.y, st.z);
        ImU32 col = st.pinza_abierta ? IM_COL32(30, 170, 60, 255) : IM_COL32(205, 45, 45, 255);
        dl->AddCircleFilled(p, 6.0f, col);
        dl->AddCircle(p, 6.0f, IM_COL32(40, 40, 40, 255), 0, 1.5f);
    }

    ImGui::Dummy(tamano_canvas);
}

static void ajustar_ventana_a_limites_utiles(SDL_Window* window) {
    if (!window) return;
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN || flags & SDL_WINDOW_FULLSCREEN_DESKTOP || flags & SDL_WINDOW_MAXIMIZED) {
        return;
    }

    int indice_pantalla = SDL_GetWindowDisplayIndex(window);
    if (indice_pantalla < 0) return;

    SDL_Rect limites_utiles = {};
    if (SDL_GetDisplayUsableBounds(indice_pantalla, &limites_utiles) != 0) return;

    int x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return;

    int max_x = limites_utiles.x + limites_utiles.w - w;
    int max_y = limites_utiles.y + limites_utiles.h - h;
    int x_ajustado = x;
    int y_ajustado = y;

    if (max_x >= limites_utiles.x) {
        if (x_ajustado < limites_utiles.x) x_ajustado = limites_utiles.x;
        if (x_ajustado > max_x) x_ajustado = max_x;
    }
    // Mantener visible el borde superior aunque la ventana sea mas grande que el area util.
    if (y_ajustado < limites_utiles.y) y_ajustado = limites_utiles.y;
    if (max_y >= limites_utiles.y && y_ajustado > max_y) y_ajustado = max_y;

    if (x_ajustado != x || y_ajustado != y) {
        SDL_SetWindowPosition(window, x_ajustado, y_ajustado);
    }
}

static int imgui_cb_captura_cursor(ImGuiInputTextCallbackData* data) {
    DatosCapturaCursor* d = reinterpret_cast<DatosCapturaCursor*>(data->UserData);
    if (d && d->cursor_pos) {
        *d->cursor_pos = data->CursorPos;
    }
    return 0;
}

static std::string recortar_copia(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

static void calcular_linea_columna(const std::string& text, int cursor_pos, int& out_line, int& out_col) {
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

static int linea_desde_offset(const std::string& text, int offset) {
    if (offset < 0) offset = 0;
    if (offset > static_cast<int>(text.size())) offset = static_cast<int>(text.size());
    int line = 1;
    for (int i = 0; i < offset; ++i) {
        if (text[i] == '\n') line++;
    }
    return line;
}

static void setear_linea_foco_tab(TabArchivoAbierto& tab, int line) {
    if (line <= 0) return;
    tab.linea_foco = line;
    tab.syntax_scroll_line = line;
}

static void saltar_a_hit_referencia(TabArchivoAbierto& tab,
                                  const std::vector<int>& coincidencias_refs,
                                  int indice_refs) {
    if (indice_refs < 0 || indice_refs >= static_cast<int>(coincidencias_refs.size())) return;
    setear_linea_foco_tab(tab, linea_desde_offset(tab.contenido, coincidencias_refs[indice_refs]));
}

static void saltar_a_referencia_con_feedback(TabArchivoAbierto& tab,
                                            EstadoUiEditor& editor_ui,
                                            int indice_refs,
                                            const char* origen,
                                            std::vector<std::string>& console_lines) {
    if (indice_refs < 0 || indice_refs >= static_cast<int>(editor_ui.coincidencias_refs.size())) return;
    editor_ui.mostrar_refs = true;
    editor_ui.indice_refs = indice_refs;
    editor_ui.origen_refs = (origen && origen[0] != '\0') ? origen : "refs";
    saltar_a_hit_referencia(tab, editor_ui.coincidencias_refs, editor_ui.indice_refs);

    int total = static_cast<int>(editor_ui.coincidencias_refs.size());
    int line = linea_desde_offset(tab.contenido, editor_ui.coincidencias_refs[editor_ui.indice_refs]);
    console_lines.push_back("[IDE] Ref " + std::to_string(editor_ui.indice_refs + 1) + "/" +
                            std::to_string(total) + " -> L" + std::to_string(line) +
                            " ('" + editor_ui.simbolo_refs + "', " + editor_ui.origen_refs + ")");
}

static int offset_inicio_linea(const std::string& text, int line) {
    if (line <= 1) return 0;
    int current = 1;
    for (int i = 0; i < static_cast<int>(text.size()); ++i) {
        if (text[i] == '\n') {
            current++;
            if (current == line) return i + 1;
        }
    }
    return static_cast<int>(text.size());
}

static std::string simbolo_desde_linea(const std::string& text, int line) {
    int start = offset_inicio_linea(text, line);
    int i = start;
    const int n = static_cast<int>(text.size());
    while (i < n && text[i] != '\n') {
        if (!es_caracter_palabra(text[i])) {
            i++;
            continue;
        }
        int j = i;
        while (j < n && es_caracter_palabra(text[j])) j++;
        std::string token = text.substr(static_cast<size_t>(i), static_cast<size_t>(j - i));
        std::string up = copia_mayus(token);
        if (!es_palabra_clave_scara_mayus(up)) {
            return token;
        }
        i = j;
    }
    return "";
}

static int buscar_indice_hit_en_linea(const std::string& text,
                                  const std::vector<int>& hits,
                                  int line) {
    for (int i = 0; i < static_cast<int>(hits.size()); ++i) {
        if (linea_desde_offset(text, hits[i]) == line) return i;
    }
    return -1;
}

static void sincronizar_refs_desde_simbolo(TabArchivoAbierto& tab,
                                  EstadoUiEditor& editor_ui,
                                  const std::string& simbolo,
                                  int linea_preferida,
                                  const char* origen) {
    if (simbolo.empty()) return;
    editor_ui.mostrar_refs = true;
    editor_ui.simbolo_refs = simbolo;
    editor_ui.coincidencias_refs = buscar_offsets_usos_simbolo(tab.contenido, editor_ui.simbolo_refs);
    editor_ui.indice_refs = editor_ui.coincidencias_refs.empty() ? -1 : 0;
    editor_ui.origen_refs = (origen && origen[0] != '\0') ? origen : "manual";
    if (linea_preferida > 0 && !editor_ui.coincidencias_refs.empty()) {
        int hit_idx = buscar_indice_hit_en_linea(tab.contenido, editor_ui.coincidencias_refs, linea_preferida);
        if (hit_idx >= 0) editor_ui.indice_refs = hit_idx;
    }
}

static void abrir_item_diagnostico(const ItemDiagnostico& it,
                                 std::vector<TabArchivoAbierto>& tabs,
                                 int& pestana_activa,
                                 std::string& breadcrumb,
                                 std::vector<std::string>& console_lines,
                                 EstadoUiEditor& editor_ui) {
    if (abrir_o_activar_tab(it.file_path, tabs, pestana_activa, breadcrumb, console_lines)) {
        if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
            setear_linea_foco_tab(tabs[pestana_activa], it.line);
            std::string sym = simbolo_desde_linea(tabs[pestana_activa].contenido, it.line);
            sincronizar_refs_desde_simbolo(tabs[pestana_activa], editor_ui, sym, it.line, "problemas");
            console_lines.push_back("[IDE] Problema -> " + nombre_archivo_desde_ruta(it.file_path) +
                                    ":L" + std::to_string(it.line));
        }
    }
}

static std::string firma_diagnostico(const ItemDiagnostico& d) {
    return d.file_path + "|" + std::to_string(d.line) + "|" + d.kind + "|" + d.message;
}

static std::vector<ItemDiagnostico> construir_diagnosticos_visibles(const std::vector<ItemDiagnostico>& diagnostics,
                                                             const EstadoFiltroDiagnosticos& filtro_diagnosticos,
                                                             const EstadoPanelProblemas& panel_problemas_ui) {
    std::vector<ItemDiagnostico> diagnosticos_visibles;
    diagnosticos_visibles.reserve(diagnostics.size());
    for (const ItemDiagnostico& it : diagnostics) {
        if (!diagnostico_visible(it, filtro_diagnosticos)) continue;
        if (!diagnostico_coincide_busqueda(it, panel_problemas_ui.consulta_busqueda)) continue;
        diagnosticos_visibles.push_back(it);
    }
    std::sort(diagnosticos_visibles.begin(), diagnosticos_visibles.end(), [&](const ItemDiagnostico& a, const ItemDiagnostico& b) {
        return diagnostico_menor(a, b, panel_problemas_ui.modo_orden);
    });
    return diagnosticos_visibles;
}

static void sincronizar_seleccion_problemas(EstadoPanelProblemas& panel_problemas_ui,
                                   const std::vector<ItemDiagnostico>& diagnosticos_visibles) {
    if (diagnosticos_visibles.empty()) {
        panel_problemas_ui.indice_seleccionado = -1;
        panel_problemas_ui.firma_seleccionada.clear();
        return;
    }
    if (!panel_problemas_ui.firma_seleccionada.empty()) {
        for (int i = 0; i < static_cast<int>(diagnosticos_visibles.size()); ++i) {
            if (firma_diagnostico(diagnosticos_visibles[i]) == panel_problemas_ui.firma_seleccionada) {
                panel_problemas_ui.indice_seleccionado = i;
                return;
            }
        }
    }
    if (panel_problemas_ui.indice_seleccionado < 0 || panel_problemas_ui.indice_seleccionado >= static_cast<int>(diagnosticos_visibles.size())) {
        panel_problemas_ui.indice_seleccionado = 0;
    }
    panel_problemas_ui.firma_seleccionada = firma_diagnostico(diagnosticos_visibles[panel_problemas_ui.indice_seleccionado]);
}

static void reconstruir_cache_sintaxis(TabArchivoAbierto& tab) {
    if (!tab.syntax_cache_dirty) return;

    tab.syntax_lines.clear();
    tab.syntax_lines.reserve(256);

    const std::string& text = tab.contenido;
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

static std::string palabra_en_cursor(const std::string& text, int cursor_pos) {
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > static_cast<int>(text.size())) cursor_pos = static_cast<int>(text.size());
    if (text.empty()) return "";

    int left = cursor_pos;
    if (left > 0 && !es_caracter_palabra(text[left - 1]) && left < static_cast<int>(text.size()) && es_caracter_palabra(text[left])) {
        left++;
    }
    while (left > 0 && es_caracter_palabra(text[left - 1])) left--;

    int right = cursor_pos;
    while (right < static_cast<int>(text.size()) && es_caracter_palabra(text[right])) right++;

    if (right <= left) return "";
    return text.substr(static_cast<size_t>(left), static_cast<size_t>(right - left));
}

static std::vector<int> buscar_todas_coincidencias(const std::string& text, const std::string& consulta) {
    std::vector<int> hits;
    if (consulta.empty()) return hits;

    size_t pos = 0;
    while (true) {
        pos = text.find(consulta, pos);
        if (pos == std::string::npos) break;
        hits.push_back(static_cast<int>(pos));
        pos += consulta.size();
    }
    return hits;
}

static bool es_caracter_palabra(char c) {
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static std::vector<int> buscar_offsets_usos_simbolo(const std::string& text, const std::string& simbolo) {
    std::vector<int> hits;
    if (simbolo.empty()) return hits;

    std::string sym = copia_mayus(simbolo);
    const int n = static_cast<int>(text.size());
    const int m = static_cast<int>(sym.size());
    if (m <= 0 || m > n) return hits;

    for (int i = 0; i + m <= n; ++i) {
        bool at_start = (i == 0) || !es_caracter_palabra(text[i - 1]);
        bool at_end = (i + m == n) || !es_caracter_palabra(text[i + m]);
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

static std::string copia_mayus(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
        return static_cast<char>(c);
    });
    return s;
}

static std::string prefijo_palabra_actual(const std::string& text, int cursor_pos, int& out_prefix_len) {
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > static_cast<int>(text.size())) cursor_pos = static_cast<int>(text.size());

    int i = cursor_pos - 1;
    while (i >= 0 && es_caracter_palabra(text[i])) i--;
    int start = i + 1;
    out_prefix_len = cursor_pos - start;
    if (out_prefix_len <= 0) {
        out_prefix_len = 0;
        return "";
    }
    return text.substr(start, out_prefix_len);
}

static void insertar_autocompletado_en_cursor(std::string& text,
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

static std::string nombre_archivo_desde_ruta(const std::string& ruta) {
    std::filesystem::path p(ruta);
    return p.filename().string();
}

static std::string normalizar_ruta(std::string ruta) {
    std::replace(ruta.begin(), ruta.end(), '\\', '/');
    return ruta;
}

static std::string breadcrumb_desde_ruta(const std::string& ruta) {
    std::string out = ruta;
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
    return result.empty() ? ruta : result;
}

static bool leer_archivo_texto(const std::string& ruta, std::string& out_text, std::string& out_error) {
    std::ifstream in(ruta, std::ios::binary);
    if (!in) {
        out_error = "No se pudo abrir el archivo: " + ruta;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out_text = buffer.str();
    return true;
}

static bool escribir_archivo_texto(const std::string& ruta, const std::string& text, std::string& out_error) {
    std::ofstream out(ruta, std::ios::binary | std::ios::trunc);
    if (!out) {
        out_error = "No se pudo guardar el archivo: " + ruta;
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) {
        out_error = "Error al escribir el archivo: " + ruta;
        return false;
    }
    return true;
}

static int buscar_tab_por_ruta(const std::vector<TabArchivoAbierto>& tabs, const std::string& ruta) {
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        if (tabs[i].ruta == ruta) return i;
    }
    return -1;
}

static std::string ruta_entre_comillas(const std::string& ruta) {
    std::string quoted = "\"";
    for (char c : ruta) {
        if (c == '"') quoted += '\\';
        quoted += c;
    }
    quoted += "\"";
    return quoted;
}

static int ejecutar_scara_y_capturar(const std::string& source_path, std::vector<std::string>& console_lines) {
    std::string cmd = "..\\programasCOMPI\\scara.exe " + ruta_entre_comillas(source_path) + " 2>&1";
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        registrar_evento_ui_operativo(console_lines,
                                      TipoEventoUi::Error,
                                      "Ejecutar",
                                      "no se pudo ejecutar scara.exe");
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

static bool buscar_linea_definicion_simbolo(const std::string& contenido,
                                        const std::string& simbolo,
                                        int& out_line,
                                        std::string& out_kind) {
    if (simbolo.empty()) return false;

    std::string sym = copia_mayus(simbolo);
    std::stringstream ss(contenido);
    std::string line;
    int line_no = 1;

    while (std::getline(ss, line)) {
        std::string t = recortar_copia(line);
        if (empieza_con(t, "#") || t.empty()) {
            line_no++;
            continue;
        }

        std::stringstream ls(t);
        std::string kw;
        std::string nombre;
        ls >> kw >> nombre;

        std::string ukw = copia_mayus(kw);
        std::string uname = copia_mayus(nombre);
        if ((ukw == "VAR" || ukw == "POINT") && uname == sym) {
            out_line = line_no;
            out_kind = ukw;
            return true;
        }

        line_no++;
    }
    return false;
}

static std::vector<ItemSimbolo> parsear_simbolos_locales(const std::string& contenido) {
    std::vector<ItemSimbolo> symbols;
    std::stringstream ss(contenido);
    std::string line;
    int line_no = 1;

    while (std::getline(ss, line)) {
        std::string t = recortar_copia(line);
        if (t.empty() || empieza_con(t, "#")) {
            line_no++;
            continue;
        }

        std::stringstream ls(t);
        std::string kw;
        std::string nombre;
        ls >> kw >> nombre;

        std::string ukw = copia_mayus(kw);
        if (ukw == "VAR" || ukw == "POINT") {
            while (!nombre.empty() && (nombre.back() == ',' || nombre.back() == ';' || nombre.back() == ':')) nombre.pop_back();
            if (!nombre.empty()) {
                ItemSimbolo it;
                it.kind = ukw;
                it.nombre = nombre;
                it.line = line_no;
                symbols.push_back(it);
            }
        }
        line_no++;
    }
    return symbols;
}

static bool simbolo_coincide_filtro(const ItemSimbolo& s, const std::string& filtro) {
    if (filtro.empty()) return true;
    std::string f = copia_mayus(filtro);
    std::string k = copia_mayus(s.kind);
    std::string n = copia_mayus(s.nombre);
    return k.find(f) != std::string::npos || n.find(f) != std::string::npos;
}

static bool buscar_info_simbolo(const std::vector<ItemSimbolo>& symbols,
                             const std::string& nombre,
                             ItemSimbolo& out_symbol) {
    if (nombre.empty()) return false;
    std::string n = copia_mayus(nombre);
    for (const ItemSimbolo& s : symbols) {
        if (copia_mayus(s.nombre) == n) {
            out_symbol = s;
            return true;
        }
    }
    return false;
}

static std::string detectar_tipo_diagnostico(const std::string& line) {
    std::string u = copia_mayus(line);
    if (u.find("ERROR") != std::string::npos) return "Error";
    if (u.find("WARN") != std::string::npos) return "Warning";
    if (u.find("INFO") != std::string::npos) return "Info";
    return "Error";
}

static bool diagnostico_visible(const ItemDiagnostico& d, const EstadoFiltroDiagnosticos& f) {
    if (d.kind == "Error") return f.mostrar_errores;
    if (d.kind == "Warning") return f.mostrar_advertencias;
    if (d.kind == "Info") return f.mostrar_info;
    return true;
}

static int rango_tipo_diagnostico(const ItemDiagnostico& d) {
    if (d.kind == "Error") return 0;
    if (d.kind == "Warning") return 1;
    if (d.kind == "Info") return 2;
    return 3;
}

static bool diagnostico_coincide_busqueda(const ItemDiagnostico& d, const std::string& consulta) {
    if (consulta.empty()) return true;
    std::string q = copia_mayus(consulta);
    std::string f = copia_mayus(nombre_archivo_desde_ruta(d.file_path));
    std::string k = copia_mayus(d.kind);
    std::string m = copia_mayus(d.message);
    return f.find(q) != std::string::npos ||
           k.find(q) != std::string::npos ||
           m.find(q) != std::string::npos ||
           std::to_string(d.line).find(q) != std::string::npos;
}

static bool diagnostico_menor(const ItemDiagnostico& a, const ItemDiagnostico& b, int modo_orden) {
    if (modo_orden == 1) {
        int ra = rango_tipo_diagnostico(a);
        int rb = rango_tipo_diagnostico(b);
        if (ra != rb) return ra < rb;
        if (a.file_path != b.file_path) return a.file_path < b.file_path;
        return a.line < b.line;
    }

    if (modo_orden == 2) {
        if (a.message != b.message) return a.message < b.message;
        if (a.file_path != b.file_path) return a.file_path < b.file_path;
        return a.line < b.line;
    }

    if (a.file_path != b.file_path) return a.file_path < b.file_path;
    if (a.line != b.line) return a.line < b.line;
    return a.message < b.message;
}

static bool parsear_valor_bool(const std::string& s, bool valor_por_defecto) {
    std::string t = copia_mayus(recortar_copia(s));
    if (t == "1" || t == "TRUE" || t == "YES") return true;
    if (t == "0" || t == "FALSE" || t == "NO") return false;
    return valor_por_defecto;
}

static int valor_hex_desde_caracter(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

static std::string codificar_cfg_seguro(const std::string& texto) {
    static const char* hex = "0123456789ABCDEF";
    std::string salida = "hex:";
    salida.reserve(4 + texto.size() * 2);
    for (unsigned char c : texto) {
        salida.push_back(hex[(c >> 4) & 0x0F]);
        salida.push_back(hex[c & 0x0F]);
    }
    return salida;
}

static std::string decodificar_cfg_seguro(const std::string& texto) {
    if (!empieza_con(texto, "hex:")) return texto;
    std::string payload = texto.substr(4);
    if ((payload.size() % 2) != 0) return texto;

    std::string salida;
    salida.reserve(payload.size() / 2);
    for (size_t i = 0; i < payload.size(); i += 2) {
        int alto = valor_hex_desde_caracter(payload[i]);
        int bajo = valor_hex_desde_caracter(payload[i + 1]);
        if (alto < 0 || bajo < 0) return texto;
        unsigned char byte = static_cast<unsigned char>((alto << 4) | bajo);
        salida.push_back(static_cast<char>(byte));
    }
    return salida;
}

static constexpr const char* k_cfg_busqueda_explorador = "busqueda_explorador";
static constexpr const char* k_cfg_problems_search = "problems_search";
static constexpr const char* k_cfg_problems_selected_signature = "problems_selected_signature";
static constexpr const char* k_cfg_filtro_simbolo = "filtro_simbolo";
static constexpr const char* k_cfg_consulta_buscar = "consulta_buscar";
static constexpr const char* k_cfg_entrada_ir_a_linea = "entrada_ir_a_linea";
static constexpr const char* k_cfg_simbolo_refs = "simbolo_refs";
static constexpr const char* k_cfg_origen_refs = "origen_refs";
static constexpr const char* k_cfg_simbolo_ir_def = "simbolo_ir_def";
static constexpr const char* k_cfg_mostrar_proyectos = "mostrar_proyectos";
static constexpr const char* k_cfg_mostrar_consola = "mostrar_consola";
static constexpr const char* k_cfg_mostrar_panel_visual = "mostrar_panel_visual";
static constexpr const char* k_cfg_mostrar_vista_rapida = "mostrar_vista_rapida";
static constexpr const char* k_cfg_filtro_scara = "filtro_scara";
static constexpr const char* k_cfg_filtro_txt = "filtro_txt";
static constexpr const char* k_cfg_filtro_codigo = "filtro_codigo";
static constexpr const char* k_cfg_filtro_md = "filtro_md";
static constexpr const char* k_cfg_mostrar_diagnosticos = "mostrar_diagnosticos";
static constexpr const char* k_cfg_diag_show_errors = "diag_show_errors";
static constexpr const char* k_cfg_diag_show_warnings = "diag_show_warnings";
static constexpr const char* k_cfg_diag_show_info = "diag_show_info";
static constexpr const char* k_cfg_problems_sort_mode = "problems_sort_mode";
static constexpr const char* k_cfg_problems_selected_index = "problems_selected_index";
static constexpr const char* k_cfg_pestana_activa = "pestana_activa";
static constexpr const char* k_cfg_cursor_pos_activo = "cursor_pos_activo";
static constexpr const char* k_cfg_linea_foco_activa = "linea_foco_activa";
static constexpr const char* k_cfg_mostrar_simbolos = "mostrar_simbolos";
static constexpr const char* k_cfg_mostrar_info_simbolo = "mostrar_info_simbolo";
static constexpr const char* k_cfg_mostrar_editor_sintaxis = "mostrar_editor_sintaxis";
static constexpr const char* k_cfg_mostrar_buscar = "mostrar_buscar";
static constexpr const char* k_cfg_indice_buscar = "indice_buscar";
static constexpr const char* k_cfg_mostrar_ir_a = "mostrar_ir_a";
static constexpr const char* k_cfg_mostrar_refs = "mostrar_refs";
static constexpr const char* k_cfg_indice_refs = "indice_refs";
static constexpr const char* k_cfg_mostrar_ir_def = "mostrar_ir_def";
static constexpr const char* k_cfg_editor_syntax_split = "editor_syntax_split";
static constexpr const char* k_cfg_snapshot_filter = "snapshot_filter";
static constexpr const char* k_cfg_snapshot_sort = "snapshot_sort";
static constexpr const char* k_cfg_snapshot_min_dist = "snapshot_min_dist";
static constexpr const char* k_cfg_snapshot_limit_max = "snapshot_limit_max";
static constexpr const char* k_cfg_snapshot_max_dist = "snapshot_max_dist";
static constexpr const char* k_cfg_compare_warn_dist = "compare_warn_dist";
static constexpr const char* k_cfg_compare_crit_dist = "compare_crit_dist";
static constexpr const char* k_cfg_compare_enabled = "compare_enabled";
static constexpr const char* k_cfg_compare_lock_step = "compare_lock_step";
static constexpr const char* k_cfg_compare_lock_progress = "compare_lock_progress";
static constexpr const char* k_cfg_compare_ghost = "compare_ghost";
static constexpr const char* k_cfg_console_follow_selection = "console_follow_selection";
static constexpr const char* k_cfg_console_mark_vm_lines = "console_mark_vm_lines";
static constexpr const char* k_cfg_console_follow_visual_step = "console_follow_visual_step";
static constexpr const char* k_cfg_console_sync_filter_with_visual = "console_sync_filter_with_visual";
static constexpr const char* k_cfg_console_auto_select_strategy = "console_auto_select_strategy";
static constexpr const char* k_cfg_console_snap_event_on_timeline_drag = "console_snap_event_on_timeline_drag";
static constexpr const char* k_cfg_console_selected_line = "console_selected_line";
static constexpr const char* k_cfg_console_filter_by_run = "console_filter_by_run";
static constexpr const char* k_cfg_console_filter_run_index = "console_filter_run_index";
static constexpr const char* k_cfg_vis_run_index = "vis_run_index";
static constexpr const char* k_cfg_vis_compare_run_index = "vis_compare_run_index";
static constexpr const char* k_cfg_vis_compare_step_index = "vis_compare_step_index";
static constexpr const char* k_cfg_vis_play = "vis_play";
static constexpr const char* k_cfg_vis_speed = "vis_speed";
static constexpr const char* k_cfg_vis_zoom = "vis_zoom";
static constexpr const char* k_cfg_vis_pan_x = "vis_pan_x";
static constexpr const char* k_cfg_vis_pan_y = "vis_pan_y";
static constexpr const char* k_cfg_snapshot_preset_slot = "snapshot_preset_slot";
static constexpr const char* k_cfg_snapshot_preset_prefijo = "snapshot_preset_";
static constexpr const char* k_cfg_snapshot_preset_valid = "valid";
static constexpr const char* k_cfg_snapshot_preset_filter = "filter";
static constexpr const char* k_cfg_snapshot_preset_sort = "sort";
static constexpr const char* k_cfg_snapshot_preset_min_dist = "min_dist";
static constexpr const char* k_cfg_snapshot_preset_limit_max = "limit_max";
static constexpr const char* k_cfg_snapshot_preset_max_dist = "max_dist";

static bool restaurar_string_cfg_seguro(const std::string& clave,
                                        const std::string& valor,
                                        const char* clave_objetivo,
                                        std::string& salida) {
    if (clave != clave_objetivo) return false;
    salida = decodificar_cfg_seguro(valor);
    return true;
}

static void guardar_string_cfg_seguro(std::ofstream& out,
                                      const char* clave,
                                      const std::string& valor) {
    out << clave << "=" << codificar_cfg_seguro(valor) << "\n";
}

static bool restaurar_bool_cfg(const std::string& clave,
                               const std::string& valor,
                               const char* clave_objetivo,
                               bool& salida) {
    if (clave != clave_objetivo) return false;
    salida = parsear_valor_bool(valor, salida);
    return true;
}

static bool parsear_int_cfg_estricto(const std::string& valor, int& salida) {
    std::string t = recortar_copia(valor);
    if (t.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long n = std::strtol(t.c_str(), &end, 10);
    if (end == t.c_str() || (end && *end != '\0') || errno == ERANGE) return false;
    if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) return false;
    salida = static_cast<int>(n);
    return true;
}

static bool parsear_float_cfg_estricto(const std::string& valor, float& salida) {
    std::string t = recortar_copia(valor);
    if (t.empty()) return false;
    errno = 0;
    char* end = nullptr;
    float n = std::strtof(t.c_str(), &end);
    if (end == t.c_str() || (end && *end != '\0') || errno == ERANGE) return false;
    if (!std::isfinite(n)) return false;
    salida = n;
    return true;
}

static void registrar_feedback_entrada_numerica(std::vector<std::string>& console_lines,
                                                const char* contexto,
                                                const char* motivo,
                                                const std::string& detalle) {
    console_lines.push_back("[IDE] Entrada numerica " + std::string(motivo ? motivo : "invalida") +
                            " (" + std::string(contexto ? contexto : "UI") + "): " + detalle);
}

static bool parsear_int_ui_con_feedback(const std::string& entrada,
                                        const char* contexto,
                                        int minimo_permitido,
                                        int& salida,
                                        std::vector<std::string>& console_lines) {
    int valor = 0;
    if (!parsear_int_cfg_estricto(entrada, valor)) {
        registrar_feedback_entrada_numerica(console_lines, contexto, "invalida", "'" + entrada + "'");
        return false;
    }
    if (valor < minimo_permitido) {
        registrar_feedback_entrada_numerica(console_lines,
                                            contexto,
                                            "fuera de rango",
                                            std::to_string(valor) + " (min=" +
                                                std::to_string(minimo_permitido) + ")");
        return false;
    }
    salida = valor;
    return true;
}

static void registrar_evento_ui_operativo(std::vector<std::string>& console_lines,
                                          TipoEventoUi tipo,
                                          const char* contexto,
                                          const std::string& detalle) {
    const char* etiqueta = (tipo == TipoEventoUi::Error) ? "ERROR" : "INFO";
    console_lines.push_back("[IDE] " + std::string(etiqueta) +
                            " (" + std::string(contexto ? contexto : "UI") + "): " + detalle);
}

static bool restaurar_int_cfg(const std::string& clave,
                              const std::string& valor,
                              const char* clave_objetivo,
                              int& salida) {
    if (clave != clave_objetivo) return false;
    parsear_int_cfg_estricto(valor, salida);
    return true;
}

static bool restaurar_float_cfg(const std::string& clave,
                                const std::string& valor,
                                const char* clave_objetivo,
                                float& salida) {
    if (clave != clave_objetivo) return false;
    parsear_float_cfg_estricto(valor, salida);
    return true;
}

static void guardar_bool_cfg(std::ofstream& out, const char* clave, bool valor) {
    out << clave << "=" << (valor ? 1 : 0) << "\n";
}

static void guardar_int_cfg(std::ofstream& out, const char* clave, int valor) {
    out << clave << "=" << valor << "\n";
}

static void guardar_float_cfg(std::ofstream& out, const char* clave, float valor) {
    out << clave << "=" << valor << "\n";
}

enum class TipoValorCfg {
    Bool,
    Int,
    Float,
    StringSeguro
};

struct CampoCargaCfg {
    const char* clave;
    TipoValorCfg tipo;
    void* destino;
};

struct CampoGuardadoCfg {
    const char* clave;
    TipoValorCfg tipo;
    const void* origen;
};

struct CampoPresetCfg {
    const char* sufijo;
    TipoValorCfg tipo;
    size_t desplazamiento;
};

static const CampoPresetCfg k_campos_preset_cfg[] = {
    {k_cfg_snapshot_preset_valid, TipoValorCfg::Bool, offsetof(PresetFiltroSnapshotEmbebido, valido)},
    {k_cfg_snapshot_preset_filter, TipoValorCfg::Int, offsetof(PresetFiltroSnapshotEmbebido, filtro)},
    {k_cfg_snapshot_preset_sort, TipoValorCfg::Int, offsetof(PresetFiltroSnapshotEmbebido, orden)},
    {k_cfg_snapshot_preset_min_dist, TipoValorCfg::Float, offsetof(PresetFiltroSnapshotEmbebido, min_dist)},
    {k_cfg_snapshot_preset_limit_max, TipoValorCfg::Bool, offsetof(PresetFiltroSnapshotEmbebido, limitar_max)},
    {k_cfg_snapshot_preset_max_dist, TipoValorCfg::Float, offsetof(PresetFiltroSnapshotEmbebido, dist_max)}
};

static bool aplicar_campo_carga_cfg(const std::string& clave,
                                    const std::string& valor,
                                    const CampoCargaCfg* campos,
                                    size_t cantidad_campos) {
    for (size_t i = 0; i < cantidad_campos; ++i) {
        const CampoCargaCfg& campo = campos[i];
        if (clave != campo.clave) continue;
        if (campo.tipo == TipoValorCfg::Bool) {
            bool* p = reinterpret_cast<bool*>(campo.destino);
            *p = parsear_valor_bool(valor, *p);
        } else if (campo.tipo == TipoValorCfg::Int) {
            int* p = reinterpret_cast<int*>(campo.destino);
            parsear_int_cfg_estricto(valor, *p);
        } else if (campo.tipo == TipoValorCfg::Float) {
            float* p = reinterpret_cast<float*>(campo.destino);
            parsear_float_cfg_estricto(valor, *p);
        } else if (campo.tipo == TipoValorCfg::StringSeguro) {
            std::string* p = reinterpret_cast<std::string*>(campo.destino);
            *p = decodificar_cfg_seguro(valor);
        }
        return true;
    }
    return false;
}

static void guardar_campo_cfg(std::ofstream& out, const CampoGuardadoCfg& campo) {
    if (campo.tipo == TipoValorCfg::Bool) {
        const bool* p = reinterpret_cast<const bool*>(campo.origen);
        guardar_bool_cfg(out, campo.clave, *p);
    } else if (campo.tipo == TipoValorCfg::Int) {
        const int* p = reinterpret_cast<const int*>(campo.origen);
        guardar_int_cfg(out, campo.clave, *p);
    } else if (campo.tipo == TipoValorCfg::Float) {
        const float* p = reinterpret_cast<const float*>(campo.origen);
        guardar_float_cfg(out, campo.clave, *p);
    } else if (campo.tipo == TipoValorCfg::StringSeguro) {
        const std::string* p = reinterpret_cast<const std::string*>(campo.origen);
        guardar_string_cfg_seguro(out, campo.clave, *p);
    }
}

static void guardar_campos_cfg(std::ofstream& out,
                               const CampoGuardadoCfg* campos,
                               size_t cantidad_campos) {
    for (size_t i = 0; i < cantidad_campos; ++i) {
        guardar_campo_cfg(out, campos[i]);
    }
}

static bool aplicar_campo_carga_preset_cfg(const std::string& clave,
                                           const std::string& valor,
                                           PresetFiltroSnapshotEmbebido (&presets)[3]) {
    const std::string prefijo = k_cfg_snapshot_preset_prefijo;
    if (!empieza_con(clave, prefijo)) return false;

    size_t p = prefijo.size();
    if (p >= clave.size() || !std::isdigit(static_cast<unsigned char>(clave[p]))) return false;
    int idx = clave[p] - '0';
    if (idx < 0 || idx >= 3) return false;
    if (p + 1 >= clave.size() || clave[p + 1] != '_') return false;

    std::string sufijo = clave.substr(p + 2);
    PresetFiltroSnapshotEmbebido& preset = presets[idx];
    char* base = reinterpret_cast<char*>(&preset);

    for (size_t i = 0; i < sizeof(k_campos_preset_cfg) / sizeof(k_campos_preset_cfg[0]); ++i) {
        const CampoPresetCfg& campo = k_campos_preset_cfg[i];
        if (sufijo != campo.sufijo) continue;
        void* destino = base + campo.desplazamiento;
        if (campo.tipo == TipoValorCfg::Bool) {
            bool* b = reinterpret_cast<bool*>(destino);
            *b = parsear_valor_bool(valor, *b);
        } else if (campo.tipo == TipoValorCfg::Int) {
            int* n = reinterpret_cast<int*>(destino);
            parsear_int_cfg_estricto(valor, *n);
        } else if (campo.tipo == TipoValorCfg::Float) {
            float* f = reinterpret_cast<float*>(destino);
            parsear_float_cfg_estricto(valor, *f);
        }
        return true;
    }
    return false;
}

static void guardar_campos_preset_cfg(std::ofstream& out,
                                      const PresetFiltroSnapshotEmbebido (&presets)[3]) {
    for (int idx = 0; idx < 3; ++idx) {
        const PresetFiltroSnapshotEmbebido& preset = presets[idx];
        const char* base = reinterpret_cast<const char*>(&preset);
        std::string prefijo = std::string(k_cfg_snapshot_preset_prefijo) + std::to_string(idx) + "_";
        for (size_t i = 0; i < sizeof(k_campos_preset_cfg) / sizeof(k_campos_preset_cfg[0]); ++i) {
            const CampoPresetCfg& campo = k_campos_preset_cfg[i];
            std::string clave = prefijo + campo.sufijo;
            const void* origen = base + campo.desplazamiento;
            if (campo.tipo == TipoValorCfg::Bool) {
                guardar_bool_cfg(out, clave.c_str(), *reinterpret_cast<const bool*>(origen));
            } else if (campo.tipo == TipoValorCfg::Int) {
                guardar_int_cfg(out, clave.c_str(), *reinterpret_cast<const int*>(origen));
            } else if (campo.tipo == TipoValorCfg::Float) {
                guardar_float_cfg(out, clave.c_str(), *reinterpret_cast<const float*>(origen));
            }
        }
    }
}

static void sanear_estado_panel_problemas(EstadoPanelProblemas& panel_problemas_ui) {
    if (panel_problemas_ui.modo_orden < 0 || panel_problemas_ui.modo_orden > 2) {
        panel_problemas_ui.modo_orden = 0;
    }
    if (panel_problemas_ui.indice_seleccionado < -1) {
        panel_problemas_ui.indice_seleccionado = -1;
    }
}

static void sanear_estado_editor(EstadoUiEditor& editor_ui) {
    if (editor_ui.indice_buscar < -1) editor_ui.indice_buscar = -1;
    if (editor_ui.origen_refs.empty()) editor_ui.origen_refs = "manual";
    editor_ui.editor_syntax_split = std::clamp(editor_ui.editor_syntax_split, 0.25f, 0.75f);
}

static void sanear_estado_visual_embebido(int& embedded_vis_snapshot_filter,
                                          int& embedded_vis_snapshot_sort,
                                          float& embedded_vis_snapshot_min_dist,
                                          bool embedded_vis_snapshot_limit_max,
                                          float& embedded_vis_snapshot_max_dist,
                                          float& embedded_vis_compare_warn_dist,
                                          float& embedded_vis_compare_crit_dist,
                                          int& embedded_vis_snapshot_preset_slot,
                                          int& console_linea_seleccionada,
                                          int& console_filter_run_index,
                                          int& embedded_vis_run_index,
                                          int& embedded_vis_compare_run_index,
                                          int& embedded_vis_compare_idx,
                                          float& embedded_vis_speed,
                                          float& embedded_vis_zoom,
                                          float& embedded_vis_pan_x,
                                          float& embedded_vis_pan_y,
                                          PresetFiltroSnapshotEmbebido (&embedded_vis_snapshot_presets)[3]) {
    if (embedded_vis_snapshot_filter < 0 || embedded_vis_snapshot_filter > 2) embedded_vis_snapshot_filter = 0;
    if (embedded_vis_snapshot_sort < 0 || embedded_vis_snapshot_sort > 3) embedded_vis_snapshot_sort = 0;
    if (embedded_vis_snapshot_min_dist < 0.0f) embedded_vis_snapshot_min_dist = 0.0f;
    if (embedded_vis_snapshot_max_dist < 0.0f) embedded_vis_snapshot_max_dist = 0.0f;
    if (embedded_vis_snapshot_limit_max && embedded_vis_snapshot_max_dist < embedded_vis_snapshot_min_dist) {
        embedded_vis_snapshot_max_dist = embedded_vis_snapshot_min_dist;
    }
    if (embedded_vis_compare_warn_dist < 1.0f) embedded_vis_compare_warn_dist = 1.0f;
    if (embedded_vis_compare_warn_dist > 200.0f) embedded_vis_compare_warn_dist = 200.0f;
    if (embedded_vis_compare_crit_dist < 1.0f) embedded_vis_compare_crit_dist = 1.0f;
    if (embedded_vis_compare_crit_dist > 300.0f) embedded_vis_compare_crit_dist = 300.0f;
    if (embedded_vis_compare_crit_dist < embedded_vis_compare_warn_dist) {
        embedded_vis_compare_crit_dist = embedded_vis_compare_warn_dist;
    }
    if (embedded_vis_snapshot_preset_slot < 0 || embedded_vis_snapshot_preset_slot > 2) {
        embedded_vis_snapshot_preset_slot = 0;
    }
    if (console_linea_seleccionada < -1) console_linea_seleccionada = -1;
    if (console_filter_run_index < 0) console_filter_run_index = 0;
    if (embedded_vis_run_index < 0) embedded_vis_run_index = 0;
    if (embedded_vis_compare_run_index < 0) embedded_vis_compare_run_index = 0;
    if (embedded_vis_compare_idx < 0) embedded_vis_compare_idx = 0;
    if (embedded_vis_speed < 0.25f) embedded_vis_speed = 0.25f;
    if (embedded_vis_speed > 3.0f) embedded_vis_speed = 3.0f;
    if (embedded_vis_zoom < 0.5f) embedded_vis_zoom = 0.5f;
    if (embedded_vis_zoom > 2.4f) embedded_vis_zoom = 2.4f;
    if (embedded_vis_pan_x < -240.0f) embedded_vis_pan_x = -240.0f;
    if (embedded_vis_pan_x > 240.0f) embedded_vis_pan_x = 240.0f;
    if (embedded_vis_pan_y < -240.0f) embedded_vis_pan_y = -240.0f;
    if (embedded_vis_pan_y > 240.0f) embedded_vis_pan_y = 240.0f;
    for (int i = 0; i < 3; ++i) {
        if (embedded_vis_snapshot_presets[i].filtro < 0 || embedded_vis_snapshot_presets[i].filtro > 2) {
            embedded_vis_snapshot_presets[i].filtro = 0;
        }
        if (embedded_vis_snapshot_presets[i].orden < 0 || embedded_vis_snapshot_presets[i].orden > 3) {
            embedded_vis_snapshot_presets[i].orden = 0;
        }
        if (embedded_vis_snapshot_presets[i].min_dist < 0.0f) embedded_vis_snapshot_presets[i].min_dist = 0.0f;
        if (embedded_vis_snapshot_presets[i].dist_max < 0.0f) embedded_vis_snapshot_presets[i].dist_max = 0.0f;
        if (embedded_vis_snapshot_presets[i].limitar_max &&
            embedded_vis_snapshot_presets[i].dist_max < embedded_vis_snapshot_presets[i].min_dist) {
            embedded_vis_snapshot_presets[i].dist_max = embedded_vis_snapshot_presets[i].min_dist;
        }
    }
}

static std::filesystem::path ruta_estado_ui_ide() {
    char exe_path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return std::filesystem::current_path() / "scara_ide_ui.cfg";
    }
    std::filesystem::path p(exe_path);
    return p.parent_path() / "scara_ide_ui.cfg";
}

static void cargar_estado_ui_ide(bool& mostrar_proyectos,
                              bool& mostrar_consola,
                              bool& mostrar_panel_visual,
                              bool& mostrar_vista_rapida,
                              std::string& busqueda_explorador,
                              bool& filtro_scara,
                              bool& filtro_txt,
                              bool& filtro_codigo,
                              bool& filtro_md,
                              bool& mostrar_diagnosticos,
                              EstadoFiltroDiagnosticos& filtro_diagnosticos,
                              EstadoPanelProblemas& panel_problemas_ui,
                              int& pestana_activa_restaurada,
                              int& cursor_pos_activo_restaurado,
                              int& linea_foco_activa_restaurada,
                              EstadoUiEditor& editor_ui,
                              int& embedded_vis_snapshot_filter,
                              int& embedded_vis_snapshot_sort,
                              float& embedded_vis_snapshot_min_dist,
                              bool& embedded_vis_snapshot_limit_max,
                              float& embedded_vis_snapshot_max_dist,
                              float& embedded_vis_compare_warn_dist,
                              float& embedded_vis_compare_crit_dist,
                              bool& embedded_vis_compare_enabled,
                              bool& embedded_vis_compare_lock_step,
                              bool& embedded_vis_compare_lock_progress,
                              bool& embedded_vis_compare_ghost,
                              bool& console_follow_selection,
                              bool& console_mark_vm_lines,
                              bool& console_follow_visual_step,
                              bool& console_sync_filter_with_visual,
                              bool& console_snap_event_on_timeline_drag,
                              int& console_auto_select_strategy,
                              int& console_linea_seleccionada,
                              bool& console_filter_by_run,
                              int& console_filter_run_index,
                              int& embedded_vis_run_index,
                              int& embedded_vis_compare_run_index,
                              int& embedded_vis_compare_idx,
                              bool& embedded_vis_play,
                              float& embedded_vis_speed,
                              float& embedded_vis_zoom,
                              float& embedded_vis_pan_x,
                              float& embedded_vis_pan_y,
                              PresetFiltroSnapshotEmbebido (&embedded_vis_snapshot_presets)[3],
                              int& embedded_vis_snapshot_preset_slot) {
    std::ifstream in(ruta_estado_ui_ide(), std::ios::binary);
    if (!in) return;

    const CampoCargaCfg campos_carga[] = {
        {k_cfg_mostrar_proyectos, TipoValorCfg::Bool, &mostrar_proyectos},
        {k_cfg_mostrar_consola, TipoValorCfg::Bool, &mostrar_consola},
        {k_cfg_mostrar_panel_visual, TipoValorCfg::Bool, &mostrar_panel_visual},
        {k_cfg_mostrar_vista_rapida, TipoValorCfg::Bool, &mostrar_vista_rapida},
        {k_cfg_busqueda_explorador, TipoValorCfg::StringSeguro, &busqueda_explorador},
        {k_cfg_filtro_scara, TipoValorCfg::Bool, &filtro_scara},
        {k_cfg_filtro_txt, TipoValorCfg::Bool, &filtro_txt},
        {k_cfg_filtro_codigo, TipoValorCfg::Bool, &filtro_codigo},
        {k_cfg_filtro_md, TipoValorCfg::Bool, &filtro_md},
        {k_cfg_mostrar_diagnosticos, TipoValorCfg::Bool, &mostrar_diagnosticos},
        {k_cfg_diag_show_errors, TipoValorCfg::Bool, &filtro_diagnosticos.mostrar_errores},
        {k_cfg_diag_show_warnings, TipoValorCfg::Bool, &filtro_diagnosticos.mostrar_advertencias},
        {k_cfg_diag_show_info, TipoValorCfg::Bool, &filtro_diagnosticos.mostrar_info},
        {k_cfg_problems_search, TipoValorCfg::StringSeguro, &panel_problemas_ui.consulta_busqueda},
        {k_cfg_problems_sort_mode, TipoValorCfg::Int, &panel_problemas_ui.modo_orden},
        {k_cfg_problems_selected_index, TipoValorCfg::Int, &panel_problemas_ui.indice_seleccionado},
        {k_cfg_problems_selected_signature, TipoValorCfg::StringSeguro, &panel_problemas_ui.firma_seleccionada},
        {k_cfg_pestana_activa, TipoValorCfg::Int, &pestana_activa_restaurada},
        {k_cfg_cursor_pos_activo, TipoValorCfg::Int, &cursor_pos_activo_restaurado},
        {k_cfg_linea_foco_activa, TipoValorCfg::Int, &linea_foco_activa_restaurada},
        {k_cfg_mostrar_simbolos, TipoValorCfg::Bool, &editor_ui.mostrar_simbolos},
        {k_cfg_filtro_simbolo, TipoValorCfg::StringSeguro, &editor_ui.filtro_simbolo},
        {k_cfg_mostrar_info_simbolo, TipoValorCfg::Bool, &editor_ui.mostrar_info_simbolo},
        {k_cfg_mostrar_editor_sintaxis, TipoValorCfg::Bool, &editor_ui.mostrar_editor_sintaxis},
        {k_cfg_mostrar_buscar, TipoValorCfg::Bool, &editor_ui.mostrar_buscar},
        {k_cfg_consulta_buscar, TipoValorCfg::StringSeguro, &editor_ui.consulta_buscar},
        {k_cfg_indice_buscar, TipoValorCfg::Int, &editor_ui.indice_buscar},
        {k_cfg_mostrar_ir_a, TipoValorCfg::Bool, &editor_ui.mostrar_ir_a},
        {k_cfg_entrada_ir_a_linea, TipoValorCfg::StringSeguro, &editor_ui.entrada_ir_a_linea},
        {k_cfg_mostrar_refs, TipoValorCfg::Bool, &editor_ui.mostrar_refs},
        {k_cfg_simbolo_refs, TipoValorCfg::StringSeguro, &editor_ui.simbolo_refs},
        {k_cfg_indice_refs, TipoValorCfg::Int, &editor_ui.indice_refs},
        {k_cfg_origen_refs, TipoValorCfg::StringSeguro, &editor_ui.origen_refs},
        {k_cfg_mostrar_ir_def, TipoValorCfg::Bool, &editor_ui.mostrar_ir_def},
        {k_cfg_simbolo_ir_def, TipoValorCfg::StringSeguro, &editor_ui.simbolo_ir_def},
        {k_cfg_editor_syntax_split, TipoValorCfg::Float, &editor_ui.editor_syntax_split},
        {k_cfg_snapshot_filter, TipoValorCfg::Int, &embedded_vis_snapshot_filter},
        {k_cfg_snapshot_sort, TipoValorCfg::Int, &embedded_vis_snapshot_sort},
        {k_cfg_snapshot_min_dist, TipoValorCfg::Float, &embedded_vis_snapshot_min_dist},
        {k_cfg_snapshot_limit_max, TipoValorCfg::Bool, &embedded_vis_snapshot_limit_max},
        {k_cfg_snapshot_max_dist, TipoValorCfg::Float, &embedded_vis_snapshot_max_dist},
        {k_cfg_compare_warn_dist, TipoValorCfg::Float, &embedded_vis_compare_warn_dist},
        {k_cfg_compare_crit_dist, TipoValorCfg::Float, &embedded_vis_compare_crit_dist},
        {k_cfg_compare_enabled, TipoValorCfg::Bool, &embedded_vis_compare_enabled},
        {k_cfg_compare_lock_step, TipoValorCfg::Bool, &embedded_vis_compare_lock_step},
        {k_cfg_compare_lock_progress, TipoValorCfg::Bool, &embedded_vis_compare_lock_progress},
        {k_cfg_compare_ghost, TipoValorCfg::Bool, &embedded_vis_compare_ghost},
        {k_cfg_console_follow_selection, TipoValorCfg::Bool, &console_follow_selection},
        {k_cfg_console_mark_vm_lines, TipoValorCfg::Bool, &console_mark_vm_lines},
        {k_cfg_console_follow_visual_step, TipoValorCfg::Bool, &console_follow_visual_step},
        {k_cfg_console_sync_filter_with_visual, TipoValorCfg::Bool, &console_sync_filter_with_visual},
        {k_cfg_console_snap_event_on_timeline_drag, TipoValorCfg::Bool, &console_snap_event_on_timeline_drag},
        {k_cfg_console_auto_select_strategy, TipoValorCfg::Int, &console_auto_select_strategy},
        {k_cfg_console_selected_line, TipoValorCfg::Int, &console_linea_seleccionada},
        {k_cfg_console_filter_by_run, TipoValorCfg::Bool, &console_filter_by_run},
        {k_cfg_console_filter_run_index, TipoValorCfg::Int, &console_filter_run_index},
        {k_cfg_vis_run_index, TipoValorCfg::Int, &embedded_vis_run_index},
        {k_cfg_vis_compare_run_index, TipoValorCfg::Int, &embedded_vis_compare_run_index},
        {k_cfg_vis_compare_step_index, TipoValorCfg::Int, &embedded_vis_compare_idx},
        {k_cfg_vis_play, TipoValorCfg::Bool, &embedded_vis_play},
        {k_cfg_vis_speed, TipoValorCfg::Float, &embedded_vis_speed},
        {k_cfg_vis_zoom, TipoValorCfg::Float, &embedded_vis_zoom},
        {k_cfg_vis_pan_x, TipoValorCfg::Float, &embedded_vis_pan_x},
        {k_cfg_vis_pan_y, TipoValorCfg::Float, &embedded_vis_pan_y},
        {k_cfg_snapshot_preset_slot, TipoValorCfg::Int, &embedded_vis_snapshot_preset_slot}
    };

    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = recortar_copia(line.substr(0, eq));
        std::string v = line.substr(eq + 1);
        aplicar_campo_carga_cfg(k, v, campos_carga, sizeof(campos_carga) / sizeof(campos_carga[0]));
        aplicar_campo_carga_preset_cfg(k, v, embedded_vis_snapshot_presets);
    }

    sanear_estado_panel_problemas(panel_problemas_ui);
    if (pestana_activa_restaurada < 0) pestana_activa_restaurada = 0;
    if (cursor_pos_activo_restaurado < 0) cursor_pos_activo_restaurado = 0;
    if (linea_foco_activa_restaurada < 1) linea_foco_activa_restaurada = 1;
    sanear_estado_editor(editor_ui);
    sanear_estado_visual_embebido(embedded_vis_snapshot_filter,
                                  embedded_vis_snapshot_sort,
                                  embedded_vis_snapshot_min_dist,
                                  embedded_vis_snapshot_limit_max,
                                  embedded_vis_snapshot_max_dist,
                                  embedded_vis_compare_warn_dist,
                                  embedded_vis_compare_crit_dist,
                                  embedded_vis_snapshot_preset_slot,
                                  console_linea_seleccionada,
                                  console_filter_run_index,
                                  embedded_vis_run_index,
                                  embedded_vis_compare_run_index,
                                  embedded_vis_compare_idx,
                                  embedded_vis_speed,
                                  embedded_vis_zoom,
                                  embedded_vis_pan_x,
                                  embedded_vis_pan_y,
                                  embedded_vis_snapshot_presets);
}

static void guardar_estado_ui_ide(bool mostrar_proyectos,
                              bool mostrar_consola,
                              bool mostrar_panel_visual,
                              bool mostrar_vista_rapida,
                              const std::string& busqueda_explorador,
                              bool filtro_scara,
                              bool filtro_txt,
                              bool filtro_codigo,
                              bool filtro_md,
                              bool mostrar_diagnosticos,
                              const EstadoFiltroDiagnosticos& filtro_diagnosticos,
                              const EstadoPanelProblemas& panel_problemas_ui,
                              int pestana_activa_guardada,
                              int cursor_pos_activo_guardado,
                              int linea_foco_activa_guardada,
                              const EstadoUiEditor& editor_ui,
                              int embedded_vis_snapshot_filter,
                              int embedded_vis_snapshot_sort,
                              float embedded_vis_snapshot_min_dist,
                              bool embedded_vis_snapshot_limit_max,
                              float embedded_vis_snapshot_max_dist,
                              float embedded_vis_compare_warn_dist,
                              float embedded_vis_compare_crit_dist,
                              bool embedded_vis_compare_enabled,
                              bool embedded_vis_compare_lock_step,
                              bool embedded_vis_compare_lock_progress,
                              bool embedded_vis_compare_ghost,
                              bool console_follow_selection,
                              bool console_mark_vm_lines,
                              bool console_follow_visual_step,
                              bool console_sync_filter_with_visual,
                              bool console_snap_event_on_timeline_drag,
                              int console_auto_select_strategy,
                              int console_linea_seleccionada_guardada,
                              bool console_filter_by_run,
                              int console_filter_run_index,
                              int embedded_vis_run_index,
                              int embedded_vis_compare_run_index,
                              int embedded_vis_compare_idx,
                              bool embedded_vis_play,
                              float embedded_vis_speed,
                              float embedded_vis_zoom,
                              float embedded_vis_pan_x,
                              float embedded_vis_pan_y,
                              const PresetFiltroSnapshotEmbebido (&embedded_vis_snapshot_presets)[3],
                              int embedded_vis_snapshot_preset_slot) {
    std::ofstream out(ruta_estado_ui_ide(), std::ios::binary | std::ios::trunc);
    if (!out) return;

    const CampoGuardadoCfg campos_guardado[] = {
        {k_cfg_mostrar_proyectos, TipoValorCfg::Bool, &mostrar_proyectos},
        {k_cfg_mostrar_consola, TipoValorCfg::Bool, &mostrar_consola},
        {k_cfg_mostrar_panel_visual, TipoValorCfg::Bool, &mostrar_panel_visual},
        {k_cfg_mostrar_vista_rapida, TipoValorCfg::Bool, &mostrar_vista_rapida},
        {k_cfg_busqueda_explorador, TipoValorCfg::StringSeguro, &busqueda_explorador},
        {k_cfg_filtro_scara, TipoValorCfg::Bool, &filtro_scara},
        {k_cfg_filtro_txt, TipoValorCfg::Bool, &filtro_txt},
        {k_cfg_filtro_codigo, TipoValorCfg::Bool, &filtro_codigo},
        {k_cfg_filtro_md, TipoValorCfg::Bool, &filtro_md},
        {k_cfg_mostrar_diagnosticos, TipoValorCfg::Bool, &mostrar_diagnosticos},
        {k_cfg_diag_show_errors, TipoValorCfg::Bool, &filtro_diagnosticos.mostrar_errores},
        {k_cfg_diag_show_warnings, TipoValorCfg::Bool, &filtro_diagnosticos.mostrar_advertencias},
        {k_cfg_diag_show_info, TipoValorCfg::Bool, &filtro_diagnosticos.mostrar_info},
        {k_cfg_problems_search, TipoValorCfg::StringSeguro, &panel_problemas_ui.consulta_busqueda},
        {k_cfg_problems_sort_mode, TipoValorCfg::Int, &panel_problemas_ui.modo_orden},
        {k_cfg_problems_selected_index, TipoValorCfg::Int, &panel_problemas_ui.indice_seleccionado},
        {k_cfg_problems_selected_signature, TipoValorCfg::StringSeguro, &panel_problemas_ui.firma_seleccionada},
        {k_cfg_pestana_activa, TipoValorCfg::Int, &pestana_activa_guardada},
        {k_cfg_cursor_pos_activo, TipoValorCfg::Int, &cursor_pos_activo_guardado},
        {k_cfg_linea_foco_activa, TipoValorCfg::Int, &linea_foco_activa_guardada},
        {k_cfg_mostrar_simbolos, TipoValorCfg::Bool, &editor_ui.mostrar_simbolos},
        {k_cfg_filtro_simbolo, TipoValorCfg::StringSeguro, &editor_ui.filtro_simbolo},
        {k_cfg_mostrar_info_simbolo, TipoValorCfg::Bool, &editor_ui.mostrar_info_simbolo},
        {k_cfg_mostrar_editor_sintaxis, TipoValorCfg::Bool, &editor_ui.mostrar_editor_sintaxis},
        {k_cfg_mostrar_buscar, TipoValorCfg::Bool, &editor_ui.mostrar_buscar},
        {k_cfg_consulta_buscar, TipoValorCfg::StringSeguro, &editor_ui.consulta_buscar},
        {k_cfg_indice_buscar, TipoValorCfg::Int, &editor_ui.indice_buscar},
        {k_cfg_mostrar_ir_a, TipoValorCfg::Bool, &editor_ui.mostrar_ir_a},
        {k_cfg_entrada_ir_a_linea, TipoValorCfg::StringSeguro, &editor_ui.entrada_ir_a_linea},
        {k_cfg_mostrar_refs, TipoValorCfg::Bool, &editor_ui.mostrar_refs},
        {k_cfg_simbolo_refs, TipoValorCfg::StringSeguro, &editor_ui.simbolo_refs},
        {k_cfg_indice_refs, TipoValorCfg::Int, &editor_ui.indice_refs},
        {k_cfg_origen_refs, TipoValorCfg::StringSeguro, &editor_ui.origen_refs},
        {k_cfg_mostrar_ir_def, TipoValorCfg::Bool, &editor_ui.mostrar_ir_def},
        {k_cfg_simbolo_ir_def, TipoValorCfg::StringSeguro, &editor_ui.simbolo_ir_def},
        {k_cfg_editor_syntax_split, TipoValorCfg::Float, &editor_ui.editor_syntax_split},
        {k_cfg_snapshot_filter, TipoValorCfg::Int, &embedded_vis_snapshot_filter},
        {k_cfg_snapshot_sort, TipoValorCfg::Int, &embedded_vis_snapshot_sort},
        {k_cfg_snapshot_min_dist, TipoValorCfg::Float, &embedded_vis_snapshot_min_dist},
        {k_cfg_snapshot_limit_max, TipoValorCfg::Bool, &embedded_vis_snapshot_limit_max},
        {k_cfg_snapshot_max_dist, TipoValorCfg::Float, &embedded_vis_snapshot_max_dist},
        {k_cfg_compare_warn_dist, TipoValorCfg::Float, &embedded_vis_compare_warn_dist},
        {k_cfg_compare_crit_dist, TipoValorCfg::Float, &embedded_vis_compare_crit_dist},
        {k_cfg_compare_enabled, TipoValorCfg::Bool, &embedded_vis_compare_enabled},
        {k_cfg_compare_lock_step, TipoValorCfg::Bool, &embedded_vis_compare_lock_step},
        {k_cfg_compare_lock_progress, TipoValorCfg::Bool, &embedded_vis_compare_lock_progress},
        {k_cfg_compare_ghost, TipoValorCfg::Bool, &embedded_vis_compare_ghost},
        {k_cfg_console_follow_selection, TipoValorCfg::Bool, &console_follow_selection},
        {k_cfg_console_mark_vm_lines, TipoValorCfg::Bool, &console_mark_vm_lines},
        {k_cfg_console_follow_visual_step, TipoValorCfg::Bool, &console_follow_visual_step},
        {k_cfg_console_sync_filter_with_visual, TipoValorCfg::Bool, &console_sync_filter_with_visual},
        {k_cfg_console_snap_event_on_timeline_drag, TipoValorCfg::Bool, &console_snap_event_on_timeline_drag},
        {k_cfg_console_auto_select_strategy, TipoValorCfg::Int, &console_auto_select_strategy},
        {k_cfg_console_selected_line, TipoValorCfg::Int, &console_linea_seleccionada_guardada},
        {k_cfg_console_filter_by_run, TipoValorCfg::Bool, &console_filter_by_run},
        {k_cfg_console_filter_run_index, TipoValorCfg::Int, &console_filter_run_index},
        {k_cfg_vis_run_index, TipoValorCfg::Int, &embedded_vis_run_index},
        {k_cfg_vis_compare_run_index, TipoValorCfg::Int, &embedded_vis_compare_run_index},
        {k_cfg_vis_compare_step_index, TipoValorCfg::Int, &embedded_vis_compare_idx},
        {k_cfg_vis_play, TipoValorCfg::Bool, &embedded_vis_play},
        {k_cfg_vis_speed, TipoValorCfg::Float, &embedded_vis_speed},
        {k_cfg_vis_zoom, TipoValorCfg::Float, &embedded_vis_zoom},
        {k_cfg_vis_pan_x, TipoValorCfg::Float, &embedded_vis_pan_x},
        {k_cfg_vis_pan_y, TipoValorCfg::Float, &embedded_vis_pan_y},
        {k_cfg_snapshot_preset_slot, TipoValorCfg::Int, &embedded_vis_snapshot_preset_slot}
    };
    guardar_campos_cfg(out, campos_guardado, sizeof(campos_guardado) / sizeof(campos_guardado[0]));
    guardar_campos_preset_cfg(out, embedded_vis_snapshot_presets);
}

static void parsear_diagnosticos_desde_consola(const std::string& source_path,
                                           const std::vector<std::string>& console_lines,
                                           std::vector<ItemDiagnostico>& out_diags) {
    out_diags.clear();
    std::regex regex_linea("linea\\s+([0-9]+)", std::regex::icase);
    std::string ultimo_error = "";

    for (const std::string& line : console_lines) {
        if (line.find("ERROR") != std::string::npos || line.find("VM ERROR") != std::string::npos) {
            ultimo_error = line;
        }

        std::smatch m;
        if (std::regex_search(line, m, regex_linea)) {
            ItemDiagnostico d;
            d.file_path = source_path;
            d.line = std::stoi(m[1].str());
            d.kind = detectar_tipo_diagnostico(ultimo_error.empty() ? line : ultimo_error);
            d.message = ultimo_error.empty() ? line : ultimo_error;
            out_diags.push_back(d);
        }
    }
}

static void ejecutar_tab_activo(std::vector<TabArchivoAbierto>& tabs,
                               int pestana_activa,
                               std::vector<std::string>& console_lines,
                               std::vector<ItemDiagnostico>& diagnostics) {
    if (pestana_activa < 0 || pestana_activa >= static_cast<int>(tabs.size()) || !tabs[pestana_activa].abierta) {
        registrar_evento_ui_operativo(console_lines, TipoEventoUi::Error, "Ejecutar", "no hay archivo activo.");
        return;
    }

    console_lines.push_back("[IDE] Ejecutando: " + tabs[pestana_activa].ruta);
    ejecutar_scara_y_capturar(tabs[pestana_activa].ruta, console_lines);
    parsear_diagnosticos_desde_consola(tabs[pestana_activa].ruta, console_lines, diagnostics);
}

static bool abrir_dialogo_archivo_nativo(std::string& out_path) {
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
        out_path = normalizar_ruta(file_buf);
        return true;
    }
    return false;
}

static bool abrir_o_activar_tab(const std::string& raw_path,
                                 std::vector<TabArchivoAbierto>& tabs,
                                 int& pestana_activa,
                                 std::string& breadcrumb,
                                 std::vector<std::string>& console_lines) {
    std::string ruta = normalizar_ruta(raw_path);
    std::string contenido;
    std::string error;

    if (!leer_archivo_texto(ruta, contenido, error)) {
        registrar_evento_ui_operativo(console_lines, TipoEventoUi::Error, "Abrir", error);
        return false;
    }

    int indice_existente = buscar_tab_por_ruta(tabs, ruta);
    if (indice_existente >= 0) {
        tabs[indice_existente].abierta = true;
        pestana_activa = indice_existente;
        breadcrumb = breadcrumb_desde_ruta(ruta);
        registrar_evento_ui_operativo(console_lines, TipoEventoUi::Info, "Abrir", "archivo activo: " + ruta);
        return true;
    }

    tabs.push_back({ ruta, nombre_archivo_desde_ruta(ruta), contenido, true });
    tabs.back().syntax_cache_dirty = true;
    pestana_activa = static_cast<int>(tabs.size()) - 1;
    breadcrumb = breadcrumb_desde_ruta(ruta);
    registrar_evento_ui_operativo(console_lines, TipoEventoUi::Info, "Abrir", "archivo abierto: " + ruta);
    return true;
}

static bool guardar_tab_activo(std::vector<TabArchivoAbierto>& tabs,
                            int pestana_activa,
                            std::vector<std::string>& console_lines) {
    if (pestana_activa < 0 || pestana_activa >= static_cast<int>(tabs.size()) || !tabs[pestana_activa].abierta) {
        registrar_evento_ui_operativo(console_lines, TipoEventoUi::Error, "Guardar", "no hay archivo activo.");
        return false;
    }

    std::string err;
    if (!escribir_archivo_texto(tabs[pestana_activa].ruta, tabs[pestana_activa].contenido, err)) {
        registrar_evento_ui_operativo(console_lines, TipoEventoUi::Error, "Guardar", err);
        return false;
    }

    registrar_evento_ui_operativo(console_lines, TipoEventoUi::Info, "Guardar", "archivo guardado: " + tabs[pestana_activa].ruta);
    return true;
}

static bool es_archivo_scara(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".scara" || ext == ".txt" || ext == ".c" || ext == ".h" || ext == ".md";
}

static bool ruta_coincide_filtro(const std::filesystem::path& p,
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

static void dibujar_arbol_proyecto(const std::filesystem::path& root,
                              std::vector<TabArchivoAbierto>& tabs,
                              int& pestana_activa,
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
                dibujar_arbol_proyecto(entry.path(), tabs, pestana_activa, breadcrumb, console_lines,
                                  search_filter, show_scara, show_text, show_code, show_markdown,
                                  depth + 1);
                ImGui::TreePop();
            }
        } else if (entry.is_regular_file() && es_archivo_scara(entry.path()) &&
                   ruta_coincide_filtro(entry.path(), search_filter, show_scara, show_text, show_code, show_markdown)) {
            bool selected = false;
            std::string full = normalizar_ruta(entry.path().string());
            int tab_idx = buscar_tab_por_ruta(tabs, full);
            bool modificada = (tab_idx >= 0 && tabs[tab_idx].modificada);
            std::string shown_label = label + (modificada ? " *" : "");
            if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                selected = full == tabs[pestana_activa].ruta;
            }
            if (ImGui::Selectable(shown_label.c_str(), selected)) {
                abrir_o_activar_tab(entry.path().string(), tabs, pestana_activa, breadcrumb, console_lines);
            }
        }
    }
}

static void dibujar_vista_rapida_scara(TabArchivoAbierto& tab) {
    reconstruir_cache_sintaxis(tab);
    dibujar_vista_scara_resaltada("ScaraQuickView", tab.syntax_lines, tab.linea_foco, -1, -1, 120, true);
}

static void aplicar_tema_claro_personalizado() {
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
    SDL_SetWindowBordered(window, SDL_TRUE);
    SDL_MaximizeWindow(window);

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
    aplicar_tema_claro_personalizado();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;
    bool mostrar_proyectos = true;
    bool mostrar_consola = true;
    bool mostrar_panel_visual = true;
    bool mostrar_vista_rapida = true;
    bool modo_escolar = true;
    std::string busqueda_explorador;
    bool filtro_scara = true;
    bool filtro_txt = true;
    bool filtro_codigo = true;
    bool filtro_md = true;
    bool mostrar_diagnosticos = true;
    EstadoFiltroDiagnosticos filtro_diagnosticos;
    EstadoPanelProblemas panel_problemas_ui;

    std::vector<std::string> opened_projects = {
        "scara",
        "programasSCARA"
    };
    std::filesystem::path project_root = std::filesystem::current_path().parent_path();

    std::vector<TabArchivoAbierto> tabs;
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

    int pestana_activa = 0;
    int cursor_pos_activo_restaurado = 0;
    int linea_foco_activa_restaurada = 1;
    std::string breadcrumb = "C: > LAII > Proyecto > scara > scara > programasSCARA > correctos > c1_pickandplace.scara";
    EstadoUiEditor editor_ui;
    int embedded_vis_snapshot_filter = 0; // 0=Todos, 1=Warn+, 2=Crit
    int embedded_vis_snapshot_sort = 0;   // 0=Recientes, 1=Dist desc, 2=Dist asc, 3=Pinned
    float embedded_vis_snapshot_min_dist = 0.0f;
    bool embedded_vis_snapshot_limit_max = false;
    float embedded_vis_snapshot_max_dist = 100.0f;
    float embedded_vis_compare_warn_dist = 25.0f;
    float embedded_vis_compare_crit_dist = 60.0f;
    bool embedded_vis_compare_enabled = false;
    bool embedded_vis_compare_lock_step = true;
    bool embedded_vis_compare_lock_progress = true;
    bool embedded_vis_compare_ghost = true;
    bool pref_consola_seguir_seleccion = true;
    bool pref_consola_marcar_lineas_vm = true;
    bool pref_consola_seguir_paso_visual = true;
    bool pref_consola_sync_filtro_visual = true;
    bool pref_consola_snap_evento_timeline = true;
    int pref_consola_modo_auto_seleccion = 0; // 0=mantener, 1=primero, 2=ultimo
    int pref_consola_linea_seleccionada = -1;
    bool pref_consola_filtrar_por_corrida = false;
    int pref_consola_indice_corrida_filtro = 0;
    int pref_vis_embebida_indice_corrida = 0;
    int pref_vis_embebida_indice_corrida_comparada = 0;
    int pref_vis_embebida_indice_paso_comparado = 0;
    bool pref_vis_embebida_reproduccion = true;
    float pref_vis_embebida_velocidad = 1.0f;
    float pref_vis_embebida_zoom = 1.0f;
    float pref_vis_embebida_pan_x = 0.0f;
    float pref_vis_embebida_pan_y = 0.0f;
    PresetFiltroSnapshotEmbebido embedded_vis_snapshot_presets[3];
    int embedded_vis_snapshot_preset_slot = 0;

    cargar_estado_ui_ide(mostrar_proyectos,
                      mostrar_consola,
                      mostrar_panel_visual,
                      mostrar_vista_rapida,
                      busqueda_explorador,
                      filtro_scara,
                      filtro_txt,
                      filtro_codigo,
                      filtro_md,
                      mostrar_diagnosticos,
                      filtro_diagnosticos,
                      panel_problemas_ui,
                      pestana_activa,
                      cursor_pos_activo_restaurado,
                      linea_foco_activa_restaurada,
                      editor_ui,
                      embedded_vis_snapshot_filter,
                      embedded_vis_snapshot_sort,
                      embedded_vis_snapshot_min_dist,
                      embedded_vis_snapshot_limit_max,
                      embedded_vis_snapshot_max_dist,
                      embedded_vis_compare_warn_dist,
                      embedded_vis_compare_crit_dist,
                      embedded_vis_compare_enabled,
                      embedded_vis_compare_lock_step,
                      embedded_vis_compare_lock_progress,
                      embedded_vis_compare_ghost,
                      pref_consola_seguir_seleccion,
                      pref_consola_marcar_lineas_vm,
                      pref_consola_seguir_paso_visual,
                      pref_consola_sync_filtro_visual,
                      pref_consola_snap_evento_timeline,
                      pref_consola_modo_auto_seleccion,
                      pref_consola_linea_seleccionada,
                      pref_consola_filtrar_por_corrida,
                      pref_consola_indice_corrida_filtro,
                      pref_vis_embebida_indice_corrida,
                      pref_vis_embebida_indice_corrida_comparada,
                      pref_vis_embebida_indice_paso_comparado,
                      pref_vis_embebida_reproduccion,
                      pref_vis_embebida_velocidad,
                      pref_vis_embebida_zoom,
                      pref_vis_embebida_pan_x,
                      pref_vis_embebida_pan_y,
                      embedded_vis_snapshot_presets,
                      embedded_vis_snapshot_preset_slot);

    if (pestana_activa < 0 || pestana_activa >= static_cast<int>(tabs.size())) {
        pestana_activa = tabs.empty() ? -1 : 0;
    }
    if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
        breadcrumb = breadcrumb_desde_ruta(tabs[pestana_activa].ruta);
        int tam_contenido_activo = static_cast<int>(tabs[pestana_activa].contenido.size());
        if (cursor_pos_activo_restaurado > tam_contenido_activo) {
            cursor_pos_activo_restaurado = tam_contenido_activo;
        }
        editor_ui.cursor_pos = cursor_pos_activo_restaurado;
        tabs[pestana_activa].linea_foco = linea_foco_activa_restaurada;
        tabs[pestana_activa].syntax_scroll_line = linea_foco_activa_restaurada;

        if (!editor_ui.consulta_buscar.empty()) {
            editor_ui.coincidencias_buscar = buscar_todas_coincidencias(tabs[pestana_activa].contenido, editor_ui.consulta_buscar);
            if (editor_ui.coincidencias_buscar.empty()) {
                editor_ui.indice_buscar = -1;
            } else if (editor_ui.indice_buscar < 0 || editor_ui.indice_buscar >= static_cast<int>(editor_ui.coincidencias_buscar.size())) {
                editor_ui.indice_buscar = 0;
            }
        } else {
            editor_ui.coincidencias_buscar.clear();
            editor_ui.indice_buscar = -1;
        }

        if (!editor_ui.simbolo_refs.empty()) {
            editor_ui.coincidencias_refs = buscar_offsets_usos_simbolo(tabs[pestana_activa].contenido, editor_ui.simbolo_refs);
            if (editor_ui.coincidencias_refs.empty()) {
                editor_ui.indice_refs = -1;
            } else if (editor_ui.indice_refs < 0 || editor_ui.indice_refs >= static_cast<int>(editor_ui.coincidencias_refs.size())) {
                editor_ui.indice_refs = 0;
            }
        } else {
            editor_ui.coincidencias_refs.clear();
            editor_ui.indice_refs = -1;
        }
    }

    std::vector<std::string> console_lines = {
        "[IDE] Consola inicializada.",
        "[IDE] Salida del compilador/VM se mostrara aqui."
    };
    std::vector<ItemDiagnostico> diagnostics;
    std::vector<CorridaVisualEmbebida> embedded_vis_runs;
    std::vector<EstadoUiCorridaEmbebida> embedded_vis_ui_by_run;
    std::vector<EstadoVisualEmbebido> embedded_vis;
    size_t embedded_vis_source_lines = 0;
    int embedded_vis_run_index = pref_vis_embebida_indice_corrida;
    bool console_filter_by_run = pref_consola_filtrar_por_corrida;
    int console_filter_run_index = pref_consola_indice_corrida_filtro;
    int console_linea_seleccionada = pref_consola_linea_seleccionada;
    bool console_follow_selection = pref_consola_seguir_seleccion;
    bool console_mark_vm_lines = pref_consola_marcar_lineas_vm;
    bool console_follow_visual_step = pref_consola_seguir_paso_visual;
    bool console_sync_filter_with_visual = pref_consola_sync_filtro_visual;
    bool console_snap_event_on_timeline_drag = pref_consola_snap_evento_timeline;
    int console_auto_select_strategy = pref_consola_modo_auto_seleccion;
    if (console_auto_select_strategy < 0 || console_auto_select_strategy > 2) console_auto_select_strategy = 0;
    int embedded_vis_idx = 0;
    int embedded_vis_compare_run_index = pref_vis_embebida_indice_corrida_comparada;
    int embedded_vis_compare_idx = pref_vis_embebida_indice_paso_comparado;
    std::vector<SnapshotComparacionEmbebida> embedded_vis_compare_snapshots;
    bool embedded_vis_play = pref_vis_embebida_reproduccion;
    float embedded_vis_speed = pref_vis_embebida_velocidad;
    float embedded_vis_zoom = pref_vis_embebida_zoom;
    ImVec2 embedded_vis_pan(pref_vis_embebida_pan_x, pref_vis_embebida_pan_y);
    Uint32 embedded_vis_last_step = SDL_GetTicks();
    struct ToastVisualEmbebido {
        std::string text;
        int level = 0; // 0=info, 1=error
        Uint32 until = 0;
        int repeat = 1;
    };
    std::deque<ToastVisualEmbebido> embedded_vis_toasts;

    auto encolar_toast_visual = [&](const std::string& msg, bool is_error = false) {
        const int level = is_error ? 1 : 0;
        if (!embedded_vis_toasts.empty()) {
            ToastVisualEmbebido& last = embedded_vis_toasts.back();
            if (last.level == level && last.text == msg) {
                if (last.repeat < 999) last.repeat++;
                Uint32 now = SDL_GetTicks();
                Uint32 ext = is_error ? 900u : 600u;
                Uint32 max_life = now + (is_error ? 6500u : 5000u);
                Uint32 next_until = last.until + ext;
                last.until = next_until > max_life ? max_life : next_until;
                return;
            }
        }

        ToastVisualEmbebido item;
        item.text = msg;
        item.level = level;
        item.until = SDL_GetTicks() + (is_error ? 3200u : 2200u);
        if (embedded_vis_toasts.size() >= 4) {
            if (item.level == 1) {
                // Keep error toasts by evicting the oldest info toast when possible.
                auto info_it = std::find_if(embedded_vis_toasts.begin(), embedded_vis_toasts.end(),
                                            [](const ToastVisualEmbebido& t) { return t.level == 0; });
                if (info_it != embedded_vis_toasts.end()) {
                    embedded_vis_toasts.erase(info_it);
                } else {
                    embedded_vis_toasts.pop_front();
                }
            } else {
                // Drop incoming info if queue is full of errors.
                auto info_it = std::find_if(embedded_vis_toasts.begin(), embedded_vis_toasts.end(),
                                            [](const ToastVisualEmbebido& t) { return t.level == 0; });
                if (info_it != embedded_vis_toasts.end()) {
                    embedded_vis_toasts.erase(info_it);
                } else {
                    return;
                }
            }
        }
        embedded_vis_toasts.push_back(item);
    };

    auto registrar_evento_consola_ide = [&](const std::string& msg) {
        if (!console_lines.empty()) {
            std::string prefix = msg + " (x";
            const std::string& last = console_lines.back();
            if (last == msg) {
                console_lines.back() = msg + " (x2)";
                return;
            }
            if (last.rfind(prefix, 0) == 0 && !last.empty() && last.back() == ')') {
                std::string conteo_txt = last.substr(prefix.size(), last.size() - prefix.size() - 1);
                int n = 0;
                if (parsear_int_cfg_estricto(conteo_txt, n) && n >= 2) {
                    console_lines.back() = msg + " (x" + std::to_string(n + 1) + ")";
                    return;
                }
            }
        }
        console_lines.push_back(msg);
    };

    auto asegurar_ui_corridas_embebidas = [&]() {
        if (embedded_vis_ui_by_run.size() < embedded_vis_runs.size()) {
            embedded_vis_ui_by_run.resize(embedded_vis_runs.size());
        }
    };

    auto guardar_ui_corrida_embebida = [&](int run_idx) {
        if (run_idx < 0 || run_idx >= static_cast<int>(embedded_vis_runs.size())) return;
        asegurar_ui_corridas_embebidas();
        EstadoUiCorridaEmbebida& st = embedded_vis_ui_by_run[run_idx];
        st.idx = embedded_vis_idx;
        st.zoom = embedded_vis_zoom;
        st.pan = embedded_vis_pan;
        st.valido = true;
    };

    auto cargar_ui_corrida_embebida = [&](int run_idx) {
        if (run_idx < 0 || run_idx >= static_cast<int>(embedded_vis_runs.size())) return;
        asegurar_ui_corridas_embebidas();
        embedded_vis = embedded_vis_runs[run_idx].timeline;
        EstadoUiCorridaEmbebida& st = embedded_vis_ui_by_run[run_idx];
        if (st.valido) {
            embedded_vis_idx = st.idx;
            embedded_vis_zoom = st.zoom;
            embedded_vis_pan = st.pan;
        } else {
            embedded_vis_idx = 0;
            embedded_vis_zoom = pref_vis_embebida_zoom;
            embedded_vis_pan = ImVec2(pref_vis_embebida_pan_x, pref_vis_embebida_pan_y);
        }
        if (embedded_vis_idx < 0) embedded_vis_idx = 0;
        if (embedded_vis_idx >= static_cast<int>(embedded_vis.size())) {
            embedded_vis_idx = static_cast<int>(embedded_vis.size()) - 1;
        }
        if (embedded_vis_idx < 0) embedded_vis_idx = 0;
    };

    auto seleccionar_linea_consola_corrida = [&](int run_idx, int estrategia_auto) {
        if (run_idx < 0 || run_idx >= static_cast<int>(embedded_vis_runs.size())) return;
        const CorridaVisualEmbebida& rr = embedded_vis_runs[run_idx];

        if (estrategia_auto < 0 || estrategia_auto > 2) estrategia_auto = 0;

        if (estrategia_auto == 0 &&
            console_linea_seleccionada >= rr.console_start_line &&
            console_linea_seleccionada <= rr.console_end_line) {
            return;
        }

        bool usar_ultimo = (estrategia_auto == 2);

        int linea_objetivo = -1;
        if (!rr.timeline_console_lines.empty()) {
            linea_objetivo = usar_ultimo ? rr.timeline_console_lines.back() : rr.timeline_console_lines.front();
        } else if (rr.console_start_line >= 0 && rr.console_end_line >= rr.console_start_line) {
            linea_objetivo = usar_ultimo ? rr.console_end_line : rr.console_start_line;
        }

        if (linea_objetivo >= 0 && linea_objetivo < static_cast<int>(console_lines.size())) {
            console_linea_seleccionada = linea_objetivo;
        }
    };

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
            if (event.type == SDL_WINDOWEVENT && event.window.windowID == SDL_GetWindowID(window)) {
                if (event.window.event == SDL_WINDOWEVENT_RESTORED ||
                    event.window.event == SDL_WINDOWEVENT_MOVED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    ajustar_ventana_a_limites_utiles(window);
                }
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (embedded_vis_source_lines != console_lines.size()) {
            guardar_ui_corrida_embebida(embedded_vis_run_index);
            reconstruir_corridas_vis_embebida(console_lines, embedded_vis_runs);
            embedded_vis_source_lines = console_lines.size();
            if (embedded_vis_runs.empty()) {
                embedded_vis.clear();
                embedded_vis_ui_by_run.clear();
                embedded_vis_run_index = 0;
                embedded_vis_compare_run_index = 0;
                embedded_vis_compare_idx = 0;
                console_filter_run_index = 0;
                console_linea_seleccionada = -1;
                embedded_vis_idx = 0;
            } else {
                asegurar_ui_corridas_embebidas();
                if (embedded_vis_run_index < 0) embedded_vis_run_index = 0;
                if (embedded_vis_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                    embedded_vis_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                }
                if (console_filter_run_index < 0) console_filter_run_index = 0;
                if (console_filter_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                    console_filter_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                }
                if (embedded_vis_compare_run_index < 0) embedded_vis_compare_run_index = 0;
                if (embedded_vis_compare_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                    embedded_vis_compare_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                }
                cargar_ui_corrida_embebida(embedded_vis_run_index);
                const std::vector<EstadoVisualEmbebido>& cmp_tl = embedded_vis_runs[embedded_vis_compare_run_index].timeline;
                if (embedded_vis_compare_idx < 0) embedded_vis_compare_idx = 0;
                if (embedded_vis_compare_idx >= static_cast<int>(cmp_tl.size())) {
                    embedded_vis_compare_idx = static_cast<int>(cmp_tl.size()) - 1;
                }
                if (embedded_vis_compare_idx < 0) embedded_vis_compare_idx = 0;
            }
        }

        if (embedded_vis_play && embedded_vis.size() >= 2 && embedded_vis_idx < static_cast<int>(embedded_vis.size()) - 1) {
            Uint32 now = SDL_GetTicks();
            int vel = embedded_vis[embedded_vis_idx].velocidad;
            Uint32 base_delay = static_cast<Uint32>(210 - (vel * 160) / 100);
            if (base_delay < 25) base_delay = 25;
            if (base_delay > 260) base_delay = 260;
            Uint32 delay = static_cast<Uint32>(base_delay / (embedded_vis_speed <= 0.01f ? 0.01f : embedded_vis_speed));
            if (delay < 12) delay = 12;
            if ((now - embedded_vis_last_step) >= delay) {
                embedded_vis_idx++;
                embedded_vis_last_step = now;
            }
        }

        const bool ctrl_down = (io.KeyCtrl || io.KeySuper);
        const bool alt_down = io.KeyAlt;
        const bool shift_down = io.KeyShift;
        std::vector<ItemDiagnostico> diagnosticos_atajos = construir_diagnosticos_visibles(diagnostics, filtro_diagnosticos, panel_problemas_ui);
        sincronizar_seleccion_problemas(panel_problemas_ui, diagnosticos_atajos);
        if (ctrl_down && shift_down && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            editor_ui.mostrar_simbolos = !editor_ui.mostrar_simbolos;
        } else if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            std::string selected;
            if (abrir_dialogo_archivo_nativo(selected)) {
                abrir_o_activar_tab(selected, tabs, pestana_activa, breadcrumb, console_lines);
            }
        }
        if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (guardar_tab_activo(tabs, pestana_activa, console_lines) &&
                pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                tabs[pestana_activa].modificada = false;
            }
        }
        if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            editor_ui.mostrar_buscar = true;
        }
        if (ctrl_down && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            editor_ui.mostrar_ir_a = true;
        }
#if 0
        // BLOQUE AVANZADO (NO ESCOLAR): referencias y navegacion semantica.
        if (shift_down && ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            editor_ui.mostrar_refs = true;
            editor_ui.origen_refs = "shift+f12";
            if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                editor_ui.simbolo_refs = palabra_en_cursor(tabs[pestana_activa].contenido, editor_ui.cursor_pos);
                editor_ui.coincidencias_refs = buscar_offsets_usos_simbolo(tabs[pestana_activa].contenido, editor_ui.simbolo_refs);
                editor_ui.indice_refs = editor_ui.coincidencias_refs.empty() ? -1 : 0;
                if (editor_ui.indice_refs >= 0) {
                    setear_linea_foco_tab(tabs[pestana_activa], linea_desde_offset(tabs[pestana_activa].contenido, editor_ui.coincidencias_refs[editor_ui.indice_refs]));
                }
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            editor_ui.mostrar_ir_def = true;
            if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                editor_ui.simbolo_ir_def = palabra_en_cursor(tabs[pestana_activa].contenido, editor_ui.cursor_pos);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            ejecutar_tab_activo(tabs, pestana_activa, console_lines, diagnostics);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
            if (!editor_ui.coincidencias_buscar.empty()) {
                editor_ui.indice_buscar = (editor_ui.indice_buscar + 1) % static_cast<int>(editor_ui.coincidencias_buscar.size());
                if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                    setear_linea_foco_tab(tabs[pestana_activa], linea_desde_offset(tabs[pestana_activa].contenido, editor_ui.coincidencias_buscar[editor_ui.indice_buscar]));
                }
            }
        }
        if (ctrl_down && alt_down && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()) && !editor_ui.coincidencias_refs.empty()) {
                int next_idx = (editor_ui.indice_refs + 1) % static_cast<int>(editor_ui.coincidencias_refs.size());
                saltar_a_referencia_con_feedback(tabs[pestana_activa], editor_ui, next_idx, "uso siguiente", console_lines);
            }
        }
        if (ctrl_down && alt_down && ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()) && !editor_ui.coincidencias_refs.empty()) {
                int prev_idx = (editor_ui.indice_refs - 1 + static_cast<int>(editor_ui.coincidencias_refs.size())) % static_cast<int>(editor_ui.coincidencias_refs.size());
                saltar_a_referencia_con_feedback(tabs[pestana_activa], editor_ui, prev_idx, "uso previo", console_lines);
            }
        }
        if (ctrl_down && alt_down && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                int target_line = tabs[pestana_activa].linea_foco > 0 ? tabs[pestana_activa].linea_foco : editor_ui.line;
                std::string sym = palabra_en_cursor(tabs[pestana_activa].contenido, editor_ui.cursor_pos);
                if (sym.empty()) sym = simbolo_desde_linea(tabs[pestana_activa].contenido, target_line);
                sincronizar_refs_desde_simbolo(tabs[pestana_activa], editor_ui, sym, target_line, "manual" );
            }
        }
#endif
        if (!ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_F8, false) && !diagnosticos_atajos.empty()) {
            if (shift_down) {
                panel_problemas_ui.indice_seleccionado = (panel_problemas_ui.indice_seleccionado - 1 + static_cast<int>(diagnosticos_atajos.size())) % static_cast<int>(diagnosticos_atajos.size());
            } else {
                panel_problemas_ui.indice_seleccionado = (panel_problemas_ui.indice_seleccionado + 1) % static_cast<int>(diagnosticos_atajos.size());
            }
            panel_problemas_ui.firma_seleccionada = firma_diagnostico(diagnosticos_atajos[panel_problemas_ui.indice_seleccionado]);
                abrir_item_diagnostico(diagnosticos_atajos[panel_problemas_ui.indice_seleccionado], tabs, pestana_activa, breadcrumb, console_lines, editor_ui);
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
                    if (abrir_dialogo_archivo_nativo(selected)) {
                        abrir_o_activar_tab(selected, tabs, pestana_activa, breadcrumb, console_lines);
                    }
                }
                if (ImGui::MenuItem("Guardar", "Ctrl+S", false, true)) {
                    if (guardar_tab_activo(tabs, pestana_activa, console_lines) &&
                        pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                        tabs[pestana_activa].modificada = false;
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Salir")) {
                    running = false;
                }
                ImGui::EndMenu();
            }
#if 0
            // BLOQUE AVANZADO (NO ESCOLAR): configuracion de tema y fuente.
            if (ImGui::BeginMenu("Tema")) {
                if (ImGui::MenuItem("Claro SCARA")) {
                    ImGui::StyleColorsLight();
                    aplicar_tema_claro_personalizado();
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
#endif
            if (ImGui::BeginMenu("Editar")) {
                if (ImGui::MenuItem("Buscar", "Ctrl+F", false, true)) {
                    editor_ui.mostrar_buscar = true;
                }
                if (ImGui::MenuItem("Ir a linea", "Ctrl+G", false, true)) {
                    editor_ui.mostrar_ir_a = true;
                }
#if 0
                // BLOQUE AVANZADO (NO ESCOLAR): definicion/referencias/simbolos.
                if (ImGui::MenuItem("Ir a definicion", "F12", false, pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()))) {
                    editor_ui.mostrar_ir_def = true;
                    if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                        editor_ui.simbolo_ir_def = palabra_en_cursor(tabs[pestana_activa].contenido, editor_ui.cursor_pos);
                    }
                }
                if (ImGui::MenuItem("Buscar referencias", "Shift+F12", false, pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()))) {
                    editor_ui.mostrar_refs = true;
                    editor_ui.origen_refs = "menu refs";
                    if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                        editor_ui.simbolo_refs = palabra_en_cursor(tabs[pestana_activa].contenido, editor_ui.cursor_pos);
                        editor_ui.coincidencias_refs = buscar_offsets_usos_simbolo(tabs[pestana_activa].contenido, editor_ui.simbolo_refs);
                        editor_ui.indice_refs = editor_ui.coincidencias_refs.empty() ? -1 : 0;
                        if (editor_ui.indice_refs >= 0) {
                            saltar_a_hit_referencia(tabs[pestana_activa], editor_ui.coincidencias_refs, editor_ui.indice_refs);
                        }
                    }
                }
                if (ImGui::MenuItem("Uso siguiente", "Ctrl+Alt+Down", false, pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()) && !editor_ui.coincidencias_refs.empty())) {
                    int next_idx = (editor_ui.indice_refs + 1) % static_cast<int>(editor_ui.coincidencias_refs.size());
                    saltar_a_referencia_con_feedback(tabs[pestana_activa], editor_ui, next_idx, "uso siguiente", console_lines);
                }
                if (ImGui::MenuItem("Uso previo", "Ctrl+Alt+Up", false, pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()) && !editor_ui.coincidencias_refs.empty())) {
                    int prev_idx = (editor_ui.indice_refs - 1 + static_cast<int>(editor_ui.coincidencias_refs.size())) % static_cast<int>(editor_ui.coincidencias_refs.size());
                    saltar_a_referencia_con_feedback(tabs[pestana_activa], editor_ui, prev_idx, "uso previo", console_lines);
                }
                if (ImGui::MenuItem("Sincronizar refs con contexto", "Ctrl+Alt+R", false, pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()))) {
                    int target_line = tabs[pestana_activa].linea_foco > 0 ? tabs[pestana_activa].linea_foco : editor_ui.line;
                    std::string sym = palabra_en_cursor(tabs[pestana_activa].contenido, editor_ui.cursor_pos);
                    if (sym.empty()) sym = simbolo_desde_linea(tabs[pestana_activa].contenido, target_line);
                    sincronizar_refs_desde_simbolo(tabs[pestana_activa], editor_ui, sym, target_line, "manual");
                }
                if (ImGui::MenuItem("Panel de simbolos", "Ctrl+Shift+O", editor_ui.mostrar_simbolos, true)) {
                    editor_ui.mostrar_simbolos = !editor_ui.mostrar_simbolos;
                }
                if (ImGui::MenuItem("Info de simbolo en cursor", nullptr, editor_ui.mostrar_info_simbolo, true)) {
                    editor_ui.mostrar_info_simbolo = !editor_ui.mostrar_info_simbolo;
                }
                if (ImGui::MenuItem("Resaltado sintactico (editor)", nullptr, editor_ui.mostrar_editor_sintaxis, true)) {
                    editor_ui.mostrar_editor_sintaxis = !editor_ui.mostrar_editor_sintaxis;
                }
                if (ImGui::MenuItem("Siguiente coincidencia", "F3", false, !editor_ui.coincidencias_buscar.empty())) {
                    if (!editor_ui.coincidencias_buscar.empty()) {
                        editor_ui.indice_buscar = (editor_ui.indice_buscar + 1) % static_cast<int>(editor_ui.coincidencias_buscar.size());
                        if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size())) {
                            setear_linea_foco_tab(tabs[pestana_activa], linea_desde_offset(tabs[pestana_activa].contenido, editor_ui.coincidencias_buscar[editor_ui.indice_buscar]));
                        }
                    }
                }
#endif
                if (ImGui::MenuItem("Problema siguiente", "F8", false, !diagnosticos_atajos.empty())) {
                    panel_problemas_ui.indice_seleccionado = (panel_problemas_ui.indice_seleccionado + 1) % static_cast<int>(diagnosticos_atajos.size());
                    panel_problemas_ui.firma_seleccionada = firma_diagnostico(diagnosticos_atajos[panel_problemas_ui.indice_seleccionado]);
                    abrir_item_diagnostico(diagnosticos_atajos[panel_problemas_ui.indice_seleccionado], tabs, pestana_activa, breadcrumb, console_lines, editor_ui);
                }
                if (ImGui::MenuItem("Problema previo", "Shift+F8", false, !diagnosticos_atajos.empty())) {
                    panel_problemas_ui.indice_seleccionado = (panel_problemas_ui.indice_seleccionado - 1 + static_cast<int>(diagnosticos_atajos.size())) % static_cast<int>(diagnosticos_atajos.size());
                    panel_problemas_ui.firma_seleccionada = firma_diagnostico(diagnosticos_atajos[panel_problemas_ui.indice_seleccionado]);
                    abrir_item_diagnostico(diagnosticos_atajos[panel_problemas_ui.indice_seleccionado], tabs, pestana_activa, breadcrumb, console_lines, editor_ui);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Ayuda")) {
                ImGui::MenuItem("Documentacion SCARA (proximo)", nullptr, false, false);
                ImGui::MenuItem("Atajos de teclado (proximo)", nullptr, false, false);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem("Modo escolar", nullptr, &modo_escolar);
            if (ImGui::MenuItem("Ejecutar archivo", "F5", false, pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()) && tabs[pestana_activa].abierta)) {
                ejecutar_tab_activo(tabs, pestana_activa, console_lines, diagnostics);
            }
            ImGui::EndMenuBar();
        }

        ImGui::BeginChild("BreadcrumbBar", ImVec2(0, 34), true);
        ImGui::TextUnformatted(breadcrumb.c_str());
        ImGui::EndChild();

        float console_height = mostrar_consola ? 190.0f : 0.0f;
        float body_height = ImGui::GetContentRegionAvail().y - console_height;
        if (body_height < 120.0f) body_height = 120.0f;

        ImGui::BeginChild("MainBody", ImVec2(0, body_height), false);

        if (mostrar_proyectos) {
            ImGui::BeginChild("ProjectsPanel", ImVec2(260, 0), true);
            ImGui::TextUnformatted("Proyectos Abiertos");
            ImGui::Separator();
            for (const std::string& p : opened_projects) {
                ImGui::BulletText("%s", p.c_str());
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Explorador");
            ImGui::Separator();
            ImGui::InputTextWithHint("##search", "Buscar archivo...", &busqueda_explorador);
            ImGui::Checkbox(".scara", &filtro_scara);
            ImGui::SameLine();
            ImGui::Checkbox(".txt", &filtro_txt);
            ImGui::SameLine();
            ImGui::Checkbox(".c/.h/.cpp", &filtro_codigo);
            ImGui::SameLine();
            ImGui::Checkbox(".md", &filtro_md);
            ImGui::Separator();
            dibujar_arbol_proyecto(project_root, tabs, pestana_activa, breadcrumb, console_lines,
                              busqueda_explorador, filtro_scara, filtro_txt, filtro_codigo, filtro_md);

            if (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()) && tabs[pestana_activa].abierta) {
                ImGui::Separator();
                ImGui::Text("Activo: %s", tabs[pestana_activa].nombre.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(tabs[pestana_activa].modificada ? "*" : "");
            }
            ImGui::Separator();
            if (ImGui::Button("Ocultar panel")) {
                mostrar_proyectos = false;
            }
            ImGui::EndChild();
            ImGui::SameLine();
        } else {
            if (ImGui::Button(">> Proyectos")) {
                mostrar_proyectos = true;
            }
            ImGui::SameLine();
        }

        ImGui::BeginChild("EditorAndViz", ImVec2(0, 0), false);

        if (ImGui::BeginTabBar("FileTabs")) {
            for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
                if (!tabs[i].abierta) continue;

                std::string tab_label = tabs[i].nombre + (tabs[i].modificada ? " *" : "");
                ImGuiTabItemFlags tab_flags = (i == pestana_activa) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(tab_label.c_str(), &tabs[i].abierta, tab_flags)) {
                    pestana_activa = i;
                    breadcrumb = breadcrumb_desde_ruta(tabs[i].ruta);
                    reconstruir_cache_sintaxis(tabs[i]);

                    ImGui::BeginChild("EditorRegion", ImVec2(0, mostrar_panel_visual ? 360 : 0), true);
                    ImGuiInputTextFlags editor_flags = ImGuiInputTextFlags_AllowTabInput;
                    editor_flags |= ImGuiInputTextFlags_CallbackAlways;
                    DatosCapturaCursor cap{ &editor_ui.cursor_pos };

                    if (editor_ui.mostrar_editor_sintaxis) {
                        int focus_ln = tabs[i].linea_foco > 0 ? tabs[i].linea_foco : editor_ui.line;
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
                            dibujar_vista_scara_resaltada("EditorSyntaxMainInner", tabs[i].syntax_lines, focus_ln, editor_ui.line, tabs[i].syntax_scroll_line, 0, false);
                            tabs[i].syntax_scroll_line = -1;

                            ImGui::TableNextColumn();
                            if (ImGui::InputTextMultiline("##editor", &tabs[i].contenido, ImVec2(-1, -1), editor_flags,
                                                          imgui_cb_captura_cursor, &cap)) {
                                tabs[i].modificada = true;
                                tabs[i].syntax_cache_dirty = true;
                            }
                            ImGui::EndTable();
                        }
                    } else {
                        if (ImGui::InputTextMultiline("##editor", &tabs[i].contenido, ImVec2(-1, -1), editor_flags,
                                                      imgui_cb_captura_cursor, &cap)) {
                            tabs[i].modificada = true;
                            tabs[i].syntax_cache_dirty = true;
                        }
                    }
                    calcular_linea_columna(tabs[i].contenido, editor_ui.cursor_pos, editor_ui.line, editor_ui.col);
                    ImGui::EndChild();

#if 0
                    // BLOQUE AVANZADO (NO ESCOLAR): informacion y panel de simbolos.
                    std::vector<ItemSimbolo> symbols = parsear_simbolos_locales(tabs[i].contenido);
                    if (editor_ui.mostrar_info_simbolo) {
                        std::string cursor_word = palabra_en_cursor(tabs[i].contenido, editor_ui.cursor_pos);
                        ItemSimbolo sym;
                        bool has_sym = buscar_info_simbolo(symbols, cursor_word, sym);
                        if (has_sym) {
                            int uses = static_cast<int>(buscar_offsets_usos_simbolo(tabs[i].contenido, sym.nombre).size());
                            ImGui::BeginChild("SymbolInfoBar", ImVec2(0, 34), true);
                            ImGui::Text("Simbolo: %s  |  Tipo: %s  |  Def: L%d  |  Usos: %d",
                                        sym.nombre.c_str(), sym.kind.c_str(), sym.line, uses);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Ir##symbol_info")) {
                                setear_linea_foco_tab(tabs[i], sym.line);
                            }
                            ImGui::EndChild();
                        }
                    }

                    if (editor_ui.mostrar_simbolos) {
                        ImGui::BeginChild("SymbolsPanel", ImVec2(0, 110), true);
                        ImGui::TextUnformatted("Simbolos locales");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::InputTextWithHint("##symbols_filter", "Filtrar simbolos...", &editor_ui.filtro_simbolo);
                        ImGui::SameLine();
                        ImGui::Text("%d", static_cast<int>(symbols.size()));
                        ImGui::Separator();

                        for (const ItemSimbolo& s : symbols) {
                            if (!simbolo_coincide_filtro(s, editor_ui.filtro_simbolo)) continue;

                            if (s.kind == "VAR") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.16f, 0.44f, 0.80f, 1.0f));
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.60f, 0.25f, 1.0f));
                            }
                            std::string label = s.kind + " " + s.nombre + "  (L" + std::to_string(s.line) + ")";
                            if (ImGui::Selectable(label.c_str(), false)) {
                                setear_linea_foco_tab(tabs[i], s.line);
                                console_lines.push_back("[IDE] Simbolo " + s.kind + " '" + s.nombre + "' en linea " + std::to_string(s.line));
                            }
                            ImGui::PopStyleColor();
                        }
                        ImGui::EndChild();
                    }
#endif

                    if (editor_ui.mostrar_buscar) {
                        ImGui::BeginChild("FindBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Buscar:");
                        ImGui::SameLine();
                        bool changed = ImGui::InputText("##consulta_buscar", &editor_ui.consulta_buscar);
                        if (changed) {
                            editor_ui.coincidencias_buscar = buscar_todas_coincidencias(tabs[i].contenido, editor_ui.consulta_buscar);
                            editor_ui.indice_buscar = editor_ui.coincidencias_buscar.empty() ? -1 : 0;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Prev") && !editor_ui.coincidencias_buscar.empty()) {
                            editor_ui.indice_buscar = (editor_ui.indice_buscar - 1 + static_cast<int>(editor_ui.coincidencias_buscar.size())) % static_cast<int>(editor_ui.coincidencias_buscar.size());
                            setear_linea_foco_tab(tabs[i], linea_desde_offset(tabs[i].contenido, editor_ui.coincidencias_buscar[editor_ui.indice_buscar]));
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Next") && !editor_ui.coincidencias_buscar.empty()) {
                            editor_ui.indice_buscar = (editor_ui.indice_buscar + 1) % static_cast<int>(editor_ui.coincidencias_buscar.size());
                            setear_linea_foco_tab(tabs[i], linea_desde_offset(tabs[i].contenido, editor_ui.coincidencias_buscar[editor_ui.indice_buscar]));
                        }
                        ImGui::SameLine();
                        ImGui::Text("%d coincidencias", static_cast<int>(editor_ui.coincidencias_buscar.size()));
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar")) {
                            editor_ui.mostrar_buscar = false;
                        }
                        ImGui::EndChild();
                    }

                    if (editor_ui.mostrar_ir_a) {
                        ImGui::BeginChild("GotoBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Ir a linea:");
                        ImGui::SameLine();
                        ImGui::InputText("##goto_line", &editor_ui.entrada_ir_a_linea);
                        ImGui::SameLine();
                        if (ImGui::Button("Ir")) {
                            int ln = 0;
                            if (parsear_int_ui_con_feedback(editor_ui.entrada_ir_a_linea,
                                                            "Ir a linea",
                                                            1,
                                                            ln,
                                                            console_lines)) {
                                setear_linea_foco_tab(tabs[i], ln);
                                console_lines.push_back("[IDE] Foco en linea " + std::to_string(ln));
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar##goto")) {
                            editor_ui.mostrar_ir_a = false;
                        }
                        ImGui::EndChild();
                    }

#if 0
                    // BLOQUE AVANZADO (NO ESCOLAR): ir a definicion y referencias.
                    if (editor_ui.mostrar_ir_def) {
                        ImGui::BeginChild("GotoDefBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Ir a definicion:");
                        ImGui::SameLine();
                        ImGui::InputText("##simbolo_ir_def", &editor_ui.simbolo_ir_def);
                        ImGui::SameLine();
                        if (ImGui::Button("Buscar##goto_def")) {
                            int def_line = -1;
                            std::string def_kind;
                            if (buscar_linea_definicion_simbolo(tabs[i].contenido, editor_ui.simbolo_ir_def, def_line, def_kind)) {
                                setear_linea_foco_tab(tabs[i], def_line);
                                sincronizar_refs_desde_simbolo(tabs[i], editor_ui, editor_ui.simbolo_ir_def, def_line, "definicion");
                                console_lines.push_back("[IDE] Definicion " + def_kind + " de '" + editor_ui.simbolo_ir_def + "' en linea " + std::to_string(def_line));
                            } else {
                                console_lines.push_back("[IDE] No se encontro definicion local para: " + editor_ui.simbolo_ir_def);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar##goto_def")) {
                            editor_ui.mostrar_ir_def = false;
                        }
                        ImGui::EndChild();
                    }

                    if (editor_ui.mostrar_refs) {
                        ImGui::BeginChild("RefsBar", ImVec2(0, 38), true);
                        ImGui::TextUnformatted("Referencias:");
                        ImGui::SameLine();
                        bool changed_refs = ImGui::InputText("##simbolo_refs", &editor_ui.simbolo_refs);
                        ImGui::SameLine();
                        if (ImGui::Button("Buscar##refs") || changed_refs) {
                            editor_ui.coincidencias_refs = buscar_offsets_usos_simbolo(tabs[i].contenido, editor_ui.simbolo_refs);
                            editor_ui.indice_refs = editor_ui.coincidencias_refs.empty() ? -1 : 0;
                            editor_ui.origen_refs = "refs bar";
                            if (editor_ui.indice_refs >= 0) {
                                saltar_a_hit_referencia(tabs[i], editor_ui.coincidencias_refs, editor_ui.indice_refs);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Prev##refs") && !editor_ui.coincidencias_refs.empty()) {
                            int prev_idx = (editor_ui.indice_refs - 1 + static_cast<int>(editor_ui.coincidencias_refs.size())) % static_cast<int>(editor_ui.coincidencias_refs.size());
                            saltar_a_referencia_con_feedback(tabs[i], editor_ui, prev_idx, "uso previo", console_lines);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Next##refs") && !editor_ui.coincidencias_refs.empty()) {
                            int next_idx = (editor_ui.indice_refs + 1) % static_cast<int>(editor_ui.coincidencias_refs.size());
                            saltar_a_referencia_con_feedback(tabs[i], editor_ui, next_idx, "uso siguiente", console_lines);
                        }
                        ImGui::SameLine();
                        int refs_total = static_cast<int>(editor_ui.coincidencias_refs.size());
                        int refs_pos = editor_ui.indice_refs >= 0 ? (editor_ui.indice_refs + 1) : 0;
                        ImGui::Text("%d refs | %d/%d | origen: %s", refs_total, refs_pos, refs_total, editor_ui.origen_refs.c_str());
                        ImGui::SameLine();
                        if (ImGui::Button("Cerrar##refs")) {
                            editor_ui.mostrar_refs = false;
                        }
                        ImGui::EndChild();

                        ImGui::BeginChild("RefsList", ImVec2(0, 92), true);
                        for (int r = 0; r < static_cast<int>(editor_ui.coincidencias_refs.size()); ++r) {
                            int line = linea_desde_offset(tabs[i].contenido, editor_ui.coincidencias_refs[r]);
                            std::string label = "L" + std::to_string(line) + "  uso #" + std::to_string(r + 1);
                            bool referencia_seleccionada = (editor_ui.indice_refs == r);
                            if (ImGui::Selectable(label.c_str(), referencia_seleccionada)) {
                                editor_ui.indice_refs = r;
                                editor_ui.origen_refs = "lista refs";
                            }
                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                                saltar_a_referencia_con_feedback(tabs[i], editor_ui, r, "lista refs", console_lines);
                            }
                        }
                        if (editor_ui.indice_refs >= static_cast<int>(editor_ui.coincidencias_refs.size())) {
                            editor_ui.indice_refs = editor_ui.coincidencias_refs.empty() ? -1 : 0;
                        }
                        if (editor_ui.indice_refs >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                            ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive()) {
                            saltar_a_referencia_con_feedback(tabs[i], editor_ui, editor_ui.indice_refs, "lista refs", console_lines);
                        }
                        ImGui::EndChild();
                    }

                    // BLOQUE AVANZADO (NO ESCOLAR): autocompletado por prefijo.
                    {
                        static const char* kwords[] = {
                            "PROGRAM", "END", "VAR", "POINT", "MOVE", "MOVEJ", "APPROACH", "DEPART",
                            "HOME", "OPEN", "CLOSE", "SPEED", "WAIT", "IF", "ELSE", "END_IF",
                            "WHILE", "END_WHILE", "REPEAT", "END_REPEAT", "PRINT", "HALT"
                        };
                        int pref_len = 0;
                        std::string pref = copia_mayus(prefijo_palabra_actual(tabs[i].contenido, editor_ui.cursor_pos, pref_len));
                        std::vector<std::string> sugg;
                        if (!pref.empty()) {
                            for (const char* kw : kwords) {
                                std::string skw = kw;
                                if (empieza_con(skw, pref) && skw != pref) {
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
                                    insertar_autocompletado_en_cursor(tabs[i].contenido, editor_ui.cursor_pos, sugg[s], pref_len);
                                    tabs[i].modificada = true;
                                }
                                ImGui::SameLine();
                            }
                            ImGui::NewLine();
                            ImGui::EndChild();
                        }
                    }
#endif

                    ImGui::Checkbox("Vista rapida SCARA", &mostrar_vista_rapida);
                    ImGui::SameLine();
                    if (ImGui::Button("Guardar archivo")) {
                        if (guardar_tab_activo(tabs, i, console_lines)) {
                            tabs[i].modificada = false;
                        }
                    }
                    if (mostrar_vista_rapida) {
                        dibujar_vista_rapida_scara(tabs[i]);
                    }

                    ImGui::BeginChild("StatusBar", ImVec2(0, 24), true);
                    ImGui::Text("%s | Ln %d, Col %d | %s",
                                tabs[i].nombre.c_str(),
                                editor_ui.line,
                                editor_ui.col,
                                tabs[i].modificada ? "Sin guardar" : "Guardado");
                    ImGui::EndChild();

                    if (mostrar_panel_visual) {
                        ImGui::BeginChild("VisualizationPanel", ImVec2(0, 0), true);
                        ImGui::TextUnformatted("Visualizacion Isometrica Integrada (IDE)");
                        ImGui::Separator();

                        if (embedded_vis.empty()) {
                            ImGui::TextUnformatted("Sin timeline embebida aun. Ejecuta un archivo para generar traza VM.");
                        } else {
                            ImGui::SetNextItemWidth(420.0f);
                            std::string run_label = "Run " + std::to_string(embedded_vis_run_index + 1) + "/" +
                                                    std::to_string(static_cast<int>(embedded_vis_runs.size()));
                            if (!embedded_vis_runs.empty()) {
                                const std::string& p = embedded_vis_runs[embedded_vis_run_index].source_path;
                                if (!p.empty()) run_label += " - " + nombre_archivo_desde_ruta(p);
                            }
                            if (ImGui::BeginCombo("Corrida", run_label.c_str())) {
                                for (int r = 0; r < static_cast<int>(embedded_vis_runs.size()); ++r) {
                                    std::string item = "Run " + std::to_string(r + 1);
                                    if (!embedded_vis_runs[r].source_path.empty()) {
                                        item += " - " + nombre_archivo_desde_ruta(embedded_vis_runs[r].source_path);
                                    }
                                    if (embedded_vis_runs[r].timeline.empty()) {
                                        item += embedded_vis_runs[r].saw_vm_output ? " [VM sin Pos]" : " [sin VM]";
                                    } else if (embedded_vis_runs[r].timeline.size() < 2) {
                                        item += " [incompleta]";
                                    }
                                    bool sel = (r == embedded_vis_run_index);
                                    if (ImGui::Selectable(item.c_str(), sel)) {
                                        guardar_ui_corrida_embebida(embedded_vis_run_index);
                                        embedded_vis_run_index = r;
                                        cargar_ui_corrida_embebida(embedded_vis_run_index);
                                        if (console_sync_filter_with_visual) {
                                            console_filter_by_run = true;
                                            console_filter_run_index = embedded_vis_run_index;
                                        }
                                        seleccionar_linea_consola_corrida(embedded_vis_run_index, console_auto_select_strategy);
                                        embedded_vis_last_step = SDL_GetTicks();
                                    }
                                    if (sel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            // Flujo escolar: comparacion A/B desactivada para reducir complejidad.
                            embedded_vis_compare_enabled = false;
#if 0
                            // BLOQUE AVANZADO (NO ESCOLAR): controles de comparacion A/B.
                            if (ImGui::Checkbox("Comparar A/B", &embedded_vis_compare_enabled) && !embedded_vis_compare_enabled) {
                                embedded_vis_compare_idx = 0;
                            }
                            if (embedded_vis_compare_enabled) {
                                ImGui::SameLine();
                                ImGui::Checkbox("Sync paso", &embedded_vis_compare_lock_step);
                                if (embedded_vis_compare_lock_step) {
                                    ImGui::SameLine();
                                    ImGui::Checkbox("Sync %", &embedded_vis_compare_lock_progress);
                                }
                                ImGui::SameLine();
                                ImGui::Checkbox("Ghost", &embedded_vis_compare_ghost);

                                if (embedded_vis_compare_run_index < 0) embedded_vis_compare_run_index = 0;
                                if (embedded_vis_compare_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                                    embedded_vis_compare_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                                }

                                ImGui::SetNextItemWidth(420.0f);
                                std::string cmp_label = "Run " + std::to_string(embedded_vis_compare_run_index + 1);
                                if (!embedded_vis_runs[embedded_vis_compare_run_index].source_path.empty()) {
                                    cmp_label += " - " + nombre_archivo_desde_ruta(embedded_vis_runs[embedded_vis_compare_run_index].source_path);
                                }
                                if (ImGui::BeginCombo("Comparar con", cmp_label.c_str())) {
                                    for (int r = 0; r < static_cast<int>(embedded_vis_runs.size()); ++r) {
                                        std::string item = "Run " + std::to_string(r + 1);
                                        if (!embedded_vis_runs[r].source_path.empty()) {
                                            item += " - " + nombre_archivo_desde_ruta(embedded_vis_runs[r].source_path);
                                        }
                                        bool sel = (r == embedded_vis_compare_run_index);
                                        if (ImGui::Selectable(item.c_str(), sel)) {
                                            embedded_vis_compare_run_index = r;
                                            embedded_vis_compare_idx = 0;
                                        }
                                        if (sel) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                            }
#endif

                            const CorridaVisualEmbebida& current_run = embedded_vis_runs[embedded_vis_run_index];
                            const bool corrida_tiene_traza = !embedded_vis.empty();
                            const bool corrida_tiene_puntos_suficientes = embedded_vis.size() >= 2;
                            const char* estado_corrida = "ok";
                            if (!corrida_tiene_traza) {
                                estado_corrida = current_run.saw_vm_output ? "VM sin Pos" : "sin VM";
                            } else if (!corrida_tiene_puntos_suficientes) {
                                estado_corrida = "incompleta";
                            }

                            ImGui::Text("Estado: %s | VM lineas: %d | Pos parseadas: %d",
                                        estado_corrida,
                                        current_run.vm_output_lines,
                                        current_run.vm_position_lines);
                            if (current_run.console_start_line >= 0 && current_run.console_end_line >= current_run.console_start_line) {
                                ImGui::Text("Consola: L%d..L%d",
                                            current_run.console_start_line + 1,
                                            current_run.console_end_line + 1);
                            }

                            if (ImGui::Button("Filtrar consola por esta corrida")) {
                                console_filter_by_run = true;
                                console_filter_run_index = embedded_vis_run_index;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Quitar filtro consola")) {
                                console_filter_by_run = false;
                            }
                            ImGui::SameLine();
                            ImGui::Checkbox("Seguir seleccion consola", &console_follow_selection);
                            ImGui::SameLine();
                            ImGui::Checkbox("Snap evento", &console_snap_event_on_timeline_drag);

                            if (ImGui::Button(embedded_vis_play ? "Pausa" : "Play")) {
                                if (corrida_tiene_puntos_suficientes) {
                                    embedded_vis_play = !embedded_vis_play;
                                }
                                embedded_vis_last_step = SDL_GetTicks();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Reiniciar")) {
                                embedded_vis_idx = 0;
                                embedded_vis_last_step = SDL_GetTicks();
                            }
                            ImGui::SameLine();
                            bool center_on_current = ImGui::Button("Centrar efector");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(140.0f);
                            ImGui::SliderFloat("Vel", &embedded_vis_speed, 0.25f, 3.0f, "x%.2f");

                            int max_idx = static_cast<int>(embedded_vis.size()) - 1;
                            if (max_idx < 0) max_idx = 0;
                            ImGui::SetNextItemWidth(340.0f);
                            bool timeline_changed_manually = ImGui::SliderInt("Timeline", &embedded_vis_idx, 0, max_idx);
                            if (timeline_changed_manually && console_snap_event_on_timeline_drag) {
                                int linea_objetivo = -1;
                                if (embedded_vis_idx >= 0 &&
                                    embedded_vis_idx < static_cast<int>(current_run.timeline_console_lines.size())) {
                                    linea_objetivo = current_run.timeline_console_lines[embedded_vis_idx];
                                }

                                int inicio_run = current_run.console_start_line;
                                int fin_run = current_run.console_end_line;
                                if (inicio_run < 0) inicio_run = 0;
                                if (fin_run >= static_cast<int>(console_lines.size())) fin_run = static_cast<int>(console_lines.size()) - 1;

                                if (linea_objetivo >= 0 && fin_run >= inicio_run) {
                                    int mejor_linea = -1;
                                    int mejor_dist = std::numeric_limits<int>::max();
                                    for (int li = inicio_run; li <= fin_run; ++li) {
                                        int px = 0, py = 0, pz = 0;
                                        bool is_pos = parsear_pos_vm_de_linea(console_lines[li], px, py, pz);
                                        bool is_vm = (console_lines[li].find("[VM]") != std::string::npos);
                                        if (!is_pos && !is_vm) continue;
                                        int dist = std::abs(li - linea_objetivo);
                                        if (dist < mejor_dist) {
                                            mejor_dist = dist;
                                            mejor_linea = li;
                                            if (dist == 0) break;
                                        }
                                    }
                                    if (mejor_linea >= 0) {
                                        console_linea_seleccionada = mejor_linea;
                                        if (console_sync_filter_with_visual) {
                                            console_filter_by_run = true;
                                            console_filter_run_index = embedded_vis_run_index;
                                        }
                                    }
                                }
                            }
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::SliderFloat("Zoom", &embedded_vis_zoom, 0.5f, 2.4f, "%.2f");

                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::SliderFloat("Pan X", &embedded_vis_pan.x, -240.0f, 240.0f, "%.0f");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::SliderFloat("Pan Y", &embedded_vis_pan.y, -240.0f, 240.0f, "%.0f");

                            const std::vector<EstadoVisualEmbebido>* timeline_comparada_ptr = nullptr;
                            int indice_dibujo_comparado = -1;
                            if (embedded_vis_compare_enabled && !embedded_vis_runs.empty()) {
                                if (embedded_vis_compare_run_index < 0) embedded_vis_compare_run_index = 0;
                                if (embedded_vis_compare_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                                    embedded_vis_compare_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                                }
                                const std::vector<EstadoVisualEmbebido>& cmp_tl = embedded_vis_runs[embedded_vis_compare_run_index].timeline;
                                if (!cmp_tl.empty()) {
                                    if (embedded_vis_compare_lock_step) {
                                        if (embedded_vis_compare_lock_progress) {
                                            int a_last = static_cast<int>(embedded_vis.size()) - 1;
                                            int b_last = static_cast<int>(cmp_tl.size()) - 1;
                                            if (a_last > 0 && b_last > 0) {
                                                double t = static_cast<double>(embedded_vis_idx) / static_cast<double>(a_last);
                                                indice_dibujo_comparado = static_cast<int>(std::round(t * static_cast<double>(b_last)));
                                            } else {
                                                indice_dibujo_comparado = embedded_vis_idx;
                                            }
                                        } else {
                                            indice_dibujo_comparado = embedded_vis_idx;
                                        }
                                    } else {
                                        int cmp_max = static_cast<int>(cmp_tl.size()) - 1;
                                        if (cmp_max < 0) cmp_max = 0;
                                        ImGui::SetNextItemWidth(340.0f);
                                        ImGui::SliderInt("Paso comparado", &embedded_vis_compare_idx, 0, cmp_max);
                                        indice_dibujo_comparado = embedded_vis_compare_idx;
                                    }
                                    if (indice_dibujo_comparado < 0) indice_dibujo_comparado = 0;
                                    if (indice_dibujo_comparado >= static_cast<int>(cmp_tl.size())) {
                                        indice_dibujo_comparado = static_cast<int>(cmp_tl.size()) - 1;
                                    }
                                    timeline_comparada_ptr = &cmp_tl;
                                }
                            }

                            if (corrida_tiene_traza) {
                                if (embedded_vis_idx < 0) embedded_vis_idx = 0;
                                if (embedded_vis_idx >= static_cast<int>(embedded_vis.size())) embedded_vis_idx = static_cast<int>(embedded_vis.size()) - 1;

                                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive()) {
                                    int key_step = io.KeyShift ? 10 : 1;
                                    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
                                        embedded_vis_idx -= key_step;
                                        if (embedded_vis_idx < 0) embedded_vis_idx = 0;
                                        embedded_vis_play = false;
                                        embedded_vis_last_step = SDL_GetTicks();
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
                                        embedded_vis_idx += key_step;
                                        int last_idx = static_cast<int>(embedded_vis.size()) - 1;
                                        if (embedded_vis_idx > last_idx) embedded_vis_idx = last_idx;
                                        embedded_vis_play = false;
                                        embedded_vis_last_step = SDL_GetTicks();
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                                        embedded_vis_idx = 0;
                                        embedded_vis_play = false;
                                        embedded_vis_last_step = SDL_GetTicks();
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
                                        embedded_vis_idx = static_cast<int>(embedded_vis.size()) - 1;
                                        if (embedded_vis_idx < 0) embedded_vis_idx = 0;
                                        embedded_vis_play = false;
                                        embedded_vis_last_step = SDL_GetTicks();
                                    }
                                }

                                const EstadoVisualEmbebido& st = embedded_vis[embedded_vis_idx];
                                ImGui::Text("Run %d/%d | Paso %d/%d | XYZ=(%d,%d,%d) | Pinza=%s | SPEED=%d%%",
                                            embedded_vis_run_index + 1,
                                            static_cast<int>(embedded_vis_runs.size()),
                                            embedded_vis_idx + 1,
                                            static_cast<int>(embedded_vis.size()),
                                            st.x, st.y, st.z,
                                            st.pinza_abierta ? "OPEN" : "CLOSE",
                                            st.velocidad);
                                if (!corrida_tiene_puntos_suficientes) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                                       "Corrida con traza incompleta: solo %d punto VM.",
                                                       static_cast<int>(embedded_vis.size()));
                                }
                                if (embedded_vis_compare_enabled && timeline_comparada_ptr) {
                                    // BLOQUE AVANZADO (NO ESCOLAR): analitica y snapshots A/B comentados.
                                }
                                dibujar_panel_visual_embebido(embedded_vis,
                                                         embedded_vis_idx,
                                                         embedded_vis_zoom,
                                                         embedded_vis_pan,
                                                         center_on_current,
                                                         timeline_comparada_ptr,
                                                         indice_dibujo_comparado,
                                                         embedded_vis_compare_ghost);
                            } else {
                                ImGui::Text("Run %d/%d sin traza VM util.",
                                            embedded_vis_run_index + 1,
                                            static_cast<int>(embedded_vis_runs.size()));
                                if (!current_run.source_path.empty()) {
                                    ImGui::Text("Archivo: %s", current_run.source_path.c_str());
                                }
                                if (current_run.saw_vm_output) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                                       "Se detecto salida [VM], pero no lineas [VM] Pos parseables.");
                                } else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                                       "No se detecto actividad VM en esta corrida.");
                                }
                                ImGui::TextUnformatted("Sugerencia: verifica que el programa ejecute MOVE/MOVEJ y que la VM imprima Pos.");
                            }
                        }

                        ImGui::Spacing();
                        if (ImGui::Button("Ejecutar archivo seleccionado")) {
                            pestana_activa = i;
                            ejecutar_tab_activo(tabs, pestana_activa, console_lines, diagnostics);
                            embedded_vis_source_lines = 0;
                            if (!embedded_vis_runs.empty()) {
                                guardar_ui_corrida_embebida(embedded_vis_run_index);
                                embedded_vis_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                                cargar_ui_corrida_embebida(embedded_vis_run_index);
                                if (console_sync_filter_with_visual) {
                                    console_filter_by_run = true;
                                    console_filter_run_index = embedded_vis_run_index;
                                }
                                seleccionar_linea_consola_corrida(embedded_vis_run_index, console_auto_select_strategy);
                            }
                            embedded_vis_last_step = SDL_GetTicks();
                        }
                        ImGui::SameLine();
                        ImGui::Checkbox("Panel visual activo", &mostrar_panel_visual);
                        ImGui::EndChild();
                    }

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::EndChild();
        ImGui::EndChild();

        if (mostrar_consola) {
            ImGui::BeginChild("ConsolePanel", ImVec2(0, 0), true);
            bool solicitar_evento_prev = false;
            bool solicitar_evento_next = false;
            static Uint32 consola_nav_hint_hasta = 0;
            static std::string consola_nav_hint_texto;
            ImGui::TextUnformatted("Consola");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 140);
            if (ImGui::Button("Limpiar")) {
                console_lines.clear();
                diagnostics.clear();
                console_filter_by_run = false;
                console_filter_run_index = 0;
                console_linea_seleccionada = -1;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Mostrar", &mostrar_consola);
            ImGui::Separator();

            if (!embedded_vis_runs.empty()) {
                auto linea_evento_visual_actual = [&]() -> int {
                    if (embedded_vis_run_index < 0 || embedded_vis_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                        return -1;
                    }
                    const CorridaVisualEmbebida& rr = embedded_vis_runs[embedded_vis_run_index];
                    if (rr.timeline_console_lines.empty()) return -1;
                    int idx = embedded_vis_idx;
                    if (idx < 0) idx = 0;
                    if (idx >= static_cast<int>(rr.timeline_console_lines.size())) {
                        idx = static_cast<int>(rr.timeline_console_lines.size()) - 1;
                    }
                    return rr.timeline_console_lines[idx];
                };

                ImGui::Checkbox("Filtro corrida", &console_filter_by_run);
                if (console_filter_by_run) {
                    if (console_filter_run_index < 0) console_filter_run_index = 0;
                    if (console_filter_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                        console_filter_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(300.0f);
                    std::string filter_label = "Run " + std::to_string(console_filter_run_index + 1);
                    if (!embedded_vis_runs[console_filter_run_index].source_path.empty()) {
                        filter_label += " - " + nombre_archivo_desde_ruta(embedded_vis_runs[console_filter_run_index].source_path);
                    }
                    if (ImGui::BeginCombo("##console_run_filter", filter_label.c_str())) {
                        for (int r = 0; r < static_cast<int>(embedded_vis_runs.size()); ++r) {
                            std::string item = "Run " + std::to_string(r + 1);
                            if (!embedded_vis_runs[r].source_path.empty()) {
                                item += " - " + nombre_archivo_desde_ruta(embedded_vis_runs[r].source_path);
                            }
                            bool sel = (r == console_filter_run_index);
                            if (ImGui::Selectable(item.c_str(), sel)) {
                                console_filter_run_index = r;
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                if (!modo_escolar) {
                    ImGui::SameLine();
                    ImGui::Checkbox("Marcar VM/Pos", &console_mark_vm_lines);
                    ImGui::SameLine();
                    ImGui::Checkbox("Consola sigue paso", &console_follow_visual_step);
                    if (console_follow_visual_step) {
                        ImGui::SameLine();
                        ImGui::Checkbox("Sync filtro", &console_sync_filter_with_visual);
                    }
                    ImGui::SameLine();
                    const char* opciones_sel_auto_corrida[] = { "Mantener", "Primero", "Ultimo" };
                    ImGui::SetNextItemWidth(170.0f);
                    ImGui::Combo("Sel auto", &console_auto_select_strategy, opciones_sel_auto_corrida, IM_ARRAYSIZE(opciones_sel_auto_corrida));
                }

                int inicio_controles = 0;
                int fin_controles = static_cast<int>(console_lines.size()) - 1;
                if (console_filter_by_run && !embedded_vis_runs.empty()) {
                    const CorridaVisualEmbebida& rr = embedded_vis_runs[console_filter_run_index];
                    inicio_controles = rr.console_start_line;
                    fin_controles = rr.console_end_line;
                    if (inicio_controles < 0) inicio_controles = 0;
                    if (fin_controles >= static_cast<int>(console_lines.size())) {
                        fin_controles = static_cast<int>(console_lines.size()) - 1;
                    }
                }
                int conteo_eventos_vm_pos_controles = 0;
                if (!console_lines.empty() && fin_controles >= inicio_controles) {
                    for (int li = inicio_controles; li <= fin_controles; ++li) {
                        int px = 0, py = 0, pz = 0;
                        bool is_pos_line = parsear_pos_vm_de_linea(console_lines[li], px, py, pz);
                        bool is_vm_line = (console_lines[li].find("[VM]") != std::string::npos);
                        if (is_pos_line || is_vm_line) {
                            ++conteo_eventos_vm_pos_controles;
                        }
                    }
                }
                bool botones_evento_habilitados = (conteo_eventos_vm_pos_controles > 0);
                int li_evento_actual = linea_evento_visual_actual();
                bool boton_ir_evento_habilitado =
                    (li_evento_actual >= 0 && li_evento_actual < static_cast<int>(console_lines.size()));

                ImGui::SameLine();
                if (!boton_ir_evento_habilitado) ImGui::BeginDisabled();
                if (ImGui::Button("Ir evento actual")) {
                    int li = li_evento_actual;
                    if (li >= 0 && li < static_cast<int>(console_lines.size())) {
                        if (console_sync_filter_with_visual) {
                            console_filter_by_run = true;
                            console_filter_run_index = embedded_vis_run_index;
                        }
                        console_linea_seleccionada = li;
                    }
                }
                if (!boton_ir_evento_habilitado) {
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextDisabled("(sin evento actual)");
                }
                ImGui::SameLine();
                if (!botones_evento_habilitados) ImGui::BeginDisabled();
                if (ImGui::Button("Prev evento")) {
                    solicitar_evento_prev = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Next evento")) {
                    solicitar_evento_next = true;
                }
                if (!botones_evento_habilitados) {
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextDisabled("(sin VM/Pos)");
                }
                ImGui::Separator();

                if (console_mark_vm_lines) {
                    ImGui::TextUnformatted("Leyenda:");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.10f, 0.55f, 0.18f, 1.0f), "Pos");
                    ImGui::SameLine();
                    ImGui::TextUnformatted("|");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.12f, 0.36f, 0.72f, 1.0f), "VM");
                    ImGui::SameLine();
                    ImGui::TextUnformatted("| Otros");
                    ImGui::SameLine();
                    ImGui::TextDisabled("| Alt+PgUp/PgDn: evento VM/Pos");
                    ImGui::Separator();
                }
            }

            ImGui::Checkbox("Problemas", &mostrar_diagnosticos);
            if (mostrar_diagnosticos) {
                int conteo_errores = 0;
                int conteo_advertencias = 0;
                int conteo_info = 0;
                for (const ItemDiagnostico& it : diagnostics) {
                    if (it.kind == "Error") conteo_errores++;
                    else if (it.kind == "Warning") conteo_advertencias++;
                    else if (it.kind == "Info") conteo_info++;
                }
                std::vector<ItemDiagnostico> diagnosticos_visibles = construir_diagnosticos_visibles(diagnostics, filtro_diagnosticos, panel_problemas_ui);
                sincronizar_seleccion_problemas(panel_problemas_ui, diagnosticos_visibles);

                ImGui::SameLine();
                ImGui::Text("Items: %d/%d", static_cast<int>(diagnosticos_visibles.size()), static_cast<int>(diagnostics.size()));
                ImGui::SameLine();
                ImGui::Checkbox("E", &filtro_diagnosticos.mostrar_errores);
                ImGui::SameLine();
                ImGui::Text("%d", conteo_errores);
                ImGui::SameLine();
                ImGui::Checkbox("W", &filtro_diagnosticos.mostrar_advertencias);
                ImGui::SameLine();
                ImGui::Text("%d", conteo_advertencias);
                ImGui::SameLine();
                ImGui::Checkbox("I", &filtro_diagnosticos.mostrar_info);
                ImGui::SameLine();
                ImGui::Text("%d", conteo_info);
                ImGui::SetNextItemWidth(240.0f);
                ImGui::InputTextWithHint("##problems_search", "Filtrar problemas...", &panel_problemas_ui.consulta_busqueda);
                ImGui::SameLine();
                const char* opciones_orden_problemas[] = { "Archivo/Linea", "Severidad", "Mensaje" };
                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo("##problems_sort", &panel_problemas_ui.modo_orden, opciones_orden_problemas, IM_ARRAYSIZE(opciones_orden_problemas));
                ImGui::SameLine();
                if (ImGui::Button("Prev problema") && !diagnosticos_visibles.empty()) {
                    panel_problemas_ui.indice_seleccionado = (panel_problemas_ui.indice_seleccionado - 1 + static_cast<int>(diagnosticos_visibles.size())) % static_cast<int>(diagnosticos_visibles.size());
                    panel_problemas_ui.firma_seleccionada = firma_diagnostico(diagnosticos_visibles[panel_problemas_ui.indice_seleccionado]);
                    abrir_item_diagnostico(diagnosticos_visibles[panel_problemas_ui.indice_seleccionado], tabs, pestana_activa, breadcrumb, console_lines, editor_ui);
                }
                ImGui::SameLine();
                if (ImGui::Button("Next problema") && !diagnosticos_visibles.empty()) {
                    panel_problemas_ui.indice_seleccionado = (panel_problemas_ui.indice_seleccionado + 1) % static_cast<int>(diagnosticos_visibles.size());
                    panel_problemas_ui.firma_seleccionada = firma_diagnostico(diagnosticos_visibles[panel_problemas_ui.indice_seleccionado]);
                    abrir_item_diagnostico(diagnosticos_visibles[panel_problemas_ui.indice_seleccionado], tabs, pestana_activa, breadcrumb, console_lines, editor_ui);
                }
                ImGui::BeginChild("DiagnosticsPanel", ImVec2(0, 110), true);
                for (int d = 0; d < static_cast<int>(diagnosticos_visibles.size()); ++d) {
                    const ItemDiagnostico& it = diagnosticos_visibles[d];
                    std::string label = "[" + it.kind + "] " + nombre_archivo_desde_ruta(it.file_path) + ":" + std::to_string(it.line) + "  " + it.message;
                    if (it.kind == "Error") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.18f, 0.18f, 1.0f));
                    } else if (it.kind == "Warning") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.55f, 0.10f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.18f, 0.48f, 0.82f, 1.0f));
                    }
                    bool selected_problem = (panel_problemas_ui.indice_seleccionado == d);
                    if (ImGui::Selectable(label.c_str(), selected_problem)) {
                        panel_problemas_ui.indice_seleccionado = d;
                        panel_problemas_ui.firma_seleccionada = firma_diagnostico(it);
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        panel_problemas_ui.indice_seleccionado = d;
                        panel_problemas_ui.firma_seleccionada = firma_diagnostico(it);
                        abrir_item_diagnostico(it, tabs, pestana_activa, breadcrumb, console_lines, editor_ui);
                    }
                    ImGui::PopStyleColor();
                }
                if (panel_problemas_ui.indice_seleccionado >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                    ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive()) {
                    panel_problemas_ui.firma_seleccionada = firma_diagnostico(diagnosticos_visibles[panel_problemas_ui.indice_seleccionado]);
                    abrir_item_diagnostico(diagnosticos_visibles[panel_problemas_ui.indice_seleccionado], tabs, pestana_activa, breadcrumb, console_lines, editor_ui);
                }
                ImGui::EndChild();
            }

            ImGui::BeginChild("ConsoleLog", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            int linea_foco_consola = -1;
            if (console_follow_visual_step && !embedded_vis_runs.empty()) {
                if (embedded_vis_run_index < 0) embedded_vis_run_index = 0;
                if (embedded_vis_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                    embedded_vis_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                }
                if (console_sync_filter_with_visual) {
                    console_filter_by_run = true;
                    console_filter_run_index = embedded_vis_run_index;
                }
                const CorridaVisualEmbebida& fr = embedded_vis_runs[embedded_vis_run_index];
                if (!fr.timeline_console_lines.empty()) {
                    int idx_consola = embedded_vis_idx;
                    if (idx_consola < 0) idx_consola = 0;
                    if (idx_consola >= static_cast<int>(fr.timeline_console_lines.size())) {
                        idx_consola = static_cast<int>(fr.timeline_console_lines.size()) - 1;
                    }
                    linea_foco_consola = fr.timeline_console_lines[idx_consola];
                }
            }

            // Con "Consola sigue paso", la seleccion acompana al paso visual actual.
            if (console_follow_visual_step && linea_foco_consola >= 0) {
                console_linea_seleccionada = linea_foco_consola;
            }

            static bool prev_console_filter_by_run = false;
            static int prev_console_filter_run_index = -1;
            bool cambio_filtro_consola =
                (prev_console_filter_by_run != console_filter_by_run) ||
                (console_filter_by_run && prev_console_filter_run_index != console_filter_run_index);

            int inicio_filtrado = 0;
            int fin_filtrado = static_cast<int>(console_lines.size()) - 1;
            if (console_filter_by_run && !embedded_vis_runs.empty()) {
                if (console_filter_run_index < 0) console_filter_run_index = 0;
                if (console_filter_run_index >= static_cast<int>(embedded_vis_runs.size())) {
                    console_filter_run_index = static_cast<int>(embedded_vis_runs.size()) - 1;
                }
                const CorridaVisualEmbebida& rr = embedded_vis_runs[console_filter_run_index];
                inicio_filtrado = rr.console_start_line;
                fin_filtrado = rr.console_end_line;
                if (inicio_filtrado < 0) inicio_filtrado = 0;
                if (fin_filtrado >= static_cast<int>(console_lines.size())) fin_filtrado = static_cast<int>(console_lines.size()) - 1;
                if (fin_filtrado < inicio_filtrado) {
                    ImGui::TextUnformatted("Sin lineas de consola para la corrida filtrada.");
                }
            }

            if (console_linea_seleccionada < -1) console_linea_seleccionada = -1;
            if (console_linea_seleccionada >= 0 &&
                (console_linea_seleccionada < inicio_filtrado || console_linea_seleccionada > fin_filtrado)) {
                if (linea_foco_consola >= inicio_filtrado && linea_foco_consola <= fin_filtrado) {
                    console_linea_seleccionada = linea_foco_consola;
                } else if (fin_filtrado >= inicio_filtrado) {
                    if (cambio_filtro_consola) {
                        console_linea_seleccionada =
                            (console_linea_seleccionada < inicio_filtrado) ? inicio_filtrado : fin_filtrado;
                    } else {
                        console_linea_seleccionada = -1;
                    }
                } else {
                    console_linea_seleccionada = -1;
                }
            }

            int linea_objetivo_scroll = -1;
            if (console_linea_seleccionada >= inicio_filtrado && console_linea_seleccionada <= fin_filtrado) {
                linea_objetivo_scroll = console_linea_seleccionada;
            } else if (linea_foco_consola >= inicio_filtrado && linea_foco_consola <= fin_filtrado) {
                linea_objetivo_scroll = linea_foco_consola;
            }

            prev_console_filter_by_run = console_filter_by_run;
            prev_console_filter_run_index = console_filter_run_index;

            auto sincronizar_visual_desde_linea_consola = [&](int li, bool pausar_por_seleccion) {
                if (li < 0 || li >= static_cast<int>(console_lines.size())) return;
                console_linea_seleccionada = li;

                int run_idx = -1;
                if (console_filter_by_run && !embedded_vis_runs.empty()) {
                    run_idx = console_filter_run_index;
                } else {
                    run_idx = indice_corrida_desde_linea_consola(embedded_vis_runs, li);
                }

                if (run_idx >= 0 && run_idx < static_cast<int>(embedded_vis_runs.size())) {
                    const CorridaVisualEmbebida& rr = embedded_vis_runs[run_idx];
                    int step_idx = indice_timeline_desde_linea_consola(rr, li);
                    if (step_idx >= 0 && !rr.timeline.empty()) {
                        guardar_ui_corrida_embebida(embedded_vis_run_index);
                        embedded_vis_run_index = run_idx;
                        cargar_ui_corrida_embebida(embedded_vis_run_index);
                        if (step_idx >= static_cast<int>(embedded_vis.size())) {
                            step_idx = static_cast<int>(embedded_vis.size()) - 1;
                        }
                        embedded_vis_idx = step_idx;
                        if (pausar_por_seleccion && console_follow_selection) {
                            embedded_vis_play = false;
                        }
                        embedded_vis_last_step = SDL_GetTicks();
                    }
                }
            };

            auto es_evento_vm_pos = [&](int li) {
                if (li < 0 || li >= static_cast<int>(console_lines.size())) return false;
                int px = 0, py = 0, pz = 0;
                if (parsear_pos_vm_de_linea(console_lines[li], px, py, pz)) return true;
                return console_lines[li].find("[VM]") != std::string::npos;
            };

            int conteo_eventos_vm_pos_visibles = 0;
            int indice_evento_vm_pos_actual = -1;
            int linea_evento_actual = -1;
            if (console_linea_seleccionada >= inicio_filtrado && console_linea_seleccionada <= fin_filtrado &&
                es_evento_vm_pos(console_linea_seleccionada)) {
                linea_evento_actual = console_linea_seleccionada;
            } else if (linea_foco_consola >= inicio_filtrado && linea_foco_consola <= fin_filtrado &&
                       es_evento_vm_pos(linea_foco_consola)) {
                linea_evento_actual = linea_foco_consola;
            }
            if (!console_lines.empty() && fin_filtrado >= inicio_filtrado) {
                for (int li = inicio_filtrado; li <= fin_filtrado; ++li) {
                    if (!es_evento_vm_pos(li)) continue;
                    ++conteo_eventos_vm_pos_visibles;
                    if (li == linea_evento_actual) {
                        indice_evento_vm_pos_actual = conteo_eventos_vm_pos_visibles;
                    }
                }
            }

            auto navegar_evento_vm_pos = [&](int direccion) -> bool {
                if (console_lines.empty()) return false;
                if (direccion != 1 && direccion != -1) return false;

                int inicio = inicio_filtrado;
                int fin = fin_filtrado;
                if (inicio < 0) inicio = 0;
                if (fin >= static_cast<int>(console_lines.size())) {
                    fin = static_cast<int>(console_lines.size()) - 1;
                }
                if (fin < inicio) return false;

                int inicio_busqueda = console_linea_seleccionada;
                if (inicio_busqueda < inicio || inicio_busqueda > fin) {
                    inicio_busqueda = (direccion > 0) ? (inicio - 1) : (fin + 1);
                }

                int encontrado = -1;
                if (direccion > 0) {
                    for (int li = inicio_busqueda + 1; li <= fin; ++li) {
                        if (!es_evento_vm_pos(li)) continue;
                        encontrado = li;
                        break;
                    }
                    if (encontrado < 0) {
                        for (int li = inicio; li <= inicio_busqueda && li <= fin; ++li) {
                            if (!es_evento_vm_pos(li)) continue;
                            encontrado = li;
                            break;
                        }
                    }
                } else {
                    for (int li = inicio_busqueda - 1; li >= inicio; --li) {
                        if (!es_evento_vm_pos(li)) continue;
                        encontrado = li;
                        break;
                    }
                    if (encontrado < 0) {
                        for (int li = fin; li >= inicio_busqueda && li >= inicio; --li) {
                            if (!es_evento_vm_pos(li)) continue;
                            encontrado = li;
                            break;
                        }
                    }
                }

                if (encontrado >= 0) {
                    sincronizar_visual_desde_linea_consola(encontrado, true);
                    return true;
                }
                return false;
            };

            if (solicitar_evento_prev) {
                if (!navegar_evento_vm_pos(-1)) {
                    consola_nav_hint_texto = "No hay eventos VM/Pos previos en el rango activo.";
                    consola_nav_hint_hasta = SDL_GetTicks() + 1800;
                }
            }
            if (solicitar_evento_next) {
                if (!navegar_evento_vm_pos(1)) {
                    consola_nav_hint_texto = "No hay eventos VM/Pos siguientes en el rango activo.";
                    consola_nav_hint_hasta = SDL_GetTicks() + 1800;
                }
            }

            if (alt_down && !ImGui::IsAnyItemActive()) {
                if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
                    if (!navegar_evento_vm_pos(1)) {
                        consola_nav_hint_texto = "No hay eventos VM/Pos siguientes en el rango activo.";
                        consola_nav_hint_hasta = SDL_GetTicks() + 1800;
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
                    if (!navegar_evento_vm_pos(-1)) {
                        consola_nav_hint_texto = "No hay eventos VM/Pos previos en el rango activo.";
                        consola_nav_hint_hasta = SDL_GetTicks() + 1800;
                    }
                }
            }

            if (consola_nav_hint_hasta > SDL_GetTicks() && !consola_nav_hint_texto.empty()) {
                ImGui::TextColored(ImVec4(0.85f, 0.52f, 0.08f, 1.0f), "%s", consola_nav_hint_texto.c_str());
            }
            ImGui::SameLine();
            if (indice_evento_vm_pos_actual > 0) {
                ImGui::TextDisabled("Eventos VM/Pos: %d | Actual: %d/%d",
                                   conteo_eventos_vm_pos_visibles,
                                   indice_evento_vm_pos_actual,
                                   conteo_eventos_vm_pos_visibles);
            } else {
                ImGui::TextDisabled("Eventos VM/Pos: %d | Actual: -/%d",
                                   conteo_eventos_vm_pos_visibles,
                                   conteo_eventos_vm_pos_visibles);
            }
            if (modo_escolar) {
                int conteo_errores = 0;
                int conteo_advertencias = 0;
                int conteo_info = 0;
                for (const ItemDiagnostico& it : diagnostics) {
                    if (it.kind == "Error") conteo_errores++;
                    else if (it.kind == "Warning") conteo_advertencias++;
                    else if (it.kind == "Info") conteo_info++;
                }
                int paso_actual = embedded_vis.empty() ? 0 : (embedded_vis_idx + 1);
                int pasos_total = static_cast<int>(embedded_vis.size());
                ImGui::TextDisabled("Metricas: E=%d W=%d I=%d | Paso visual: %d/%d",
                                   conteo_errores,
                                   conteo_advertencias,
                                   conteo_info,
                                   paso_actual,
                                   pasos_total);
            }

            bool scroll_enfocado_aplicado = false;
            for (int li = 0; li < static_cast<int>(console_lines.size()); ++li) {
                if (li < inicio_filtrado || li > fin_filtrado) continue;
                std::string row = "L" + std::to_string(li + 1) + "  " + console_lines[li];
                bool linea_seleccionada = (li == console_linea_seleccionada) ||
                                     (console_follow_visual_step && console_linea_seleccionada < 0 && li == linea_foco_consola);
                int px = 0, py = 0, pz = 0;
                bool is_pos_line = parsear_pos_vm_de_linea(console_lines[li], px, py, pz);
                bool is_vm_line = (console_lines[li].find("[VM]") != std::string::npos);
                if (console_mark_vm_lines) {
                    if (is_pos_line) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.55f, 0.18f, 1.0f));
                    } else if (is_vm_line) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.12f, 0.36f, 0.72f, 1.0f));
                    }
                }
                if (ImGui::Selectable(row.c_str(), linea_seleccionada)) {
                    sincronizar_visual_desde_linea_consola(li, true);
                }
                if (console_mark_vm_lines && (is_pos_line || is_vm_line)) {
                    ImGui::PopStyleColor();
                }
                if (!scroll_enfocado_aplicado && linea_objetivo_scroll == li) {
                    ImGui::SetScrollHereY(0.35f);
                    scroll_enfocado_aplicado = true;
                }
            }
            if (!scroll_enfocado_aplicado && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
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

    guardar_estado_ui_ide(mostrar_proyectos,
                      mostrar_consola,
                      mostrar_panel_visual,
                      mostrar_vista_rapida,
                      busqueda_explorador,
                      filtro_scara,
                      filtro_txt,
                      filtro_codigo,
                      filtro_md,
                      mostrar_diagnosticos,
                      filtro_diagnosticos,
                      panel_problemas_ui,
                      pestana_activa,
                      editor_ui.cursor_pos,
                      (pestana_activa >= 0 && pestana_activa < static_cast<int>(tabs.size()) && tabs[pestana_activa].linea_foco > 0)
                          ? tabs[pestana_activa].linea_foco
                          : std::max(1, editor_ui.line),
                      editor_ui,
                      embedded_vis_snapshot_filter,
                      embedded_vis_snapshot_sort,
                      embedded_vis_snapshot_min_dist,
                      embedded_vis_snapshot_limit_max,
                      embedded_vis_snapshot_max_dist,
                      embedded_vis_compare_warn_dist,
                      embedded_vis_compare_crit_dist,
                      embedded_vis_compare_enabled,
                      embedded_vis_compare_lock_step,
                      embedded_vis_compare_lock_progress,
                      embedded_vis_compare_ghost,
                      console_follow_selection,
                      console_mark_vm_lines,
                      console_follow_visual_step,
                      console_sync_filter_with_visual,
                      console_snap_event_on_timeline_drag,
                      console_auto_select_strategy,
                      console_linea_seleccionada,
                      console_filter_by_run,
                      console_filter_run_index,
                      embedded_vis_run_index,
                      embedded_vis_compare_run_index,
                      embedded_vis_compare_idx,
                      embedded_vis_play,
                      embedded_vis_speed,
                      embedded_vis_zoom,
                      embedded_vis_pan.x,
                      embedded_vis_pan.y,
                      embedded_vis_snapshot_presets,
                      embedded_vis_snapshot_preset_slot);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}