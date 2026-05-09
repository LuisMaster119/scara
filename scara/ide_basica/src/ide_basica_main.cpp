#include <SDL2/SDL.h>
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "misc/cpp/imgui_stdlib.h"

static void limitar_consola(std::vector<std::string>& consola, size_t max_lineas);
static constexpr const char* k_archivo_cfg_ide_basica = "scara_ide_basica.cfg";

static int comparar_natural(const std::string& a, const std::string& b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        char ca = a[i];
        char cb = b[j];

        bool da = (ca >= '0' && ca <= '9');
        bool db = (cb >= '0' && cb <= '9');
        if (da && db) {
            size_t i0 = i;
            size_t j0 = j;
            while (i < a.size() && a[i] >= '0' && a[i] <= '9') i++;
            while (j < b.size() && b[j] >= '0' && b[j] <= '9') j++;

            std::string na = a.substr(i0, i - i0);
            std::string nb = b.substr(j0, j - j0);

            // LUIS: Si hay bloques numericos, comparamos por valor para evitar es10 antes de es2.
            while (na.size() > 1 && na[0] == '0') na.erase(0, 1);
            while (nb.size() > 1 && nb[0] == '0') nb.erase(0, 1);

            if (na.size() != nb.size()) {
                return (na.size() < nb.size()) ? -1 : 1;
            }
            if (na != nb) {
                return (na < nb) ? -1 : 1;
            }
            continue;
        }

        char ua = (ca >= 'a' && ca <= 'z') ? static_cast<char>(ca - ('a' - 'A')) : ca;
        char ub = (cb >= 'a' && cb <= 'z') ? static_cast<char>(cb - ('a' - 'A')) : cb;
        if (ua != ub) {
            return (ua < ub) ? -1 : 1;
        }
        i++;
        j++;
    }

    if (i == a.size() && j == b.size()) return 0;
    return (i == a.size()) ? -1 : 1;
}

static std::string escapar_cfg(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '=': out += "\\="; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

static std::string desescapar_cfg(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool escape = false;
    for (char c : s) {
        if (!escape) {
            if (c == '\\') {
                escape = true;
            } else {
                out.push_back(c);
            }
            continue;
        }

        switch (c) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '=': out.push_back('='); break;
            case '\\': out.push_back('\\'); break;
            default:
                out.push_back('\\');
                out.push_back(c);
                break;
        }
        escape = false;
    }
    if (escape) {
        out.push_back('\\');
    }
    return out;
}

static int parsear_entero_cfg(const std::string& v, int fallback) {
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

static float parsear_flotante_cfg(const std::string& v, float fallback) {
    try {
        return std::stof(v);
    } catch (...) {
        return fallback;
    }
}

static bool parsear_bool_cfg(const std::string& v, bool fallback) {
    if (v == "1" || v == "true" || v == "TRUE") return true;
    if (v == "0" || v == "false" || v == "FALSE") return false;
    return fallback;
}

static void guardar_cfg_ide_basica(const std::string& ruta_cfg,
                                   bool panel_derecho_abierto,
                                   bool auto_guardar_antes_ejecutar,
                                   float proporcion_altura_consola,
                                   int filtro_consola) {
    std::ofstream out(ruta_cfg, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    // AVILA: Guardamos claves simples para que el archivo cfg sea legible y facil de depurar en clase.
    out << "panel_derecho_abierto=" << (panel_derecho_abierto ? "1" : "0") << "\n";
    out << "auto_guardar_antes_ejecutar=" << (auto_guardar_antes_ejecutar ? "1" : "0") << "\n";
    out << "proporcion_altura_consola=" << proporcion_altura_consola << "\n";
    out << "filtro_consola=" << filtro_consola << "\n";
}

static bool cargar_cfg_ide_basica(const std::string& ruta_cfg,
                                  bool& panel_derecho_abierto,
                                  bool& auto_guardar_antes_ejecutar,
                                  float& proporcion_altura_consola,
                                  int& filtro_consola) {
    std::ifstream in(ruta_cfg, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string raw_val = line.substr(eq + 1);
        std::string val = desescapar_cfg(raw_val);

        if (key == "panel_derecho_abierto") {
            panel_derecho_abierto = parsear_bool_cfg(val, panel_derecho_abierto);
        } else if (key == "auto_guardar_antes_ejecutar") {
            auto_guardar_antes_ejecutar = parsear_bool_cfg(val, auto_guardar_antes_ejecutar);
        } else if (key == "proporcion_altura_consola") {
            proporcion_altura_consola = parsear_flotante_cfg(val, proporcion_altura_consola);
        } else if (key == "filtro_consola") {
            filtro_consola = parsear_entero_cfg(val, filtro_consola);
        }
    }

    if (filtro_consola < 0 || filtro_consola > 2) {
        filtro_consola = 0;
    }
    if (proporcion_altura_consola < 0.18f || proporcion_altura_consola > 0.70f) {
        proporcion_altura_consola = 0.28f;
    }

    return true;
}

static bool linea_consola_es_error(const std::string& linea) {
    std::string up = linea;
    for (char& c : up) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }
    return (up.find("ERROR") != std::string::npos);
}

static bool linea_consola_es_contexto_error(const std::string& linea) {
    std::string up = linea;
    for (char& c : up) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }

    // AARON: En filtro de errores conservamos tambien contexto para no perder detalle pedagogico.
    if (up.find("LINEA ") != std::string::npos) return true;
    if (up.find("=== COMPILANDO:") != std::string::npos) return true;
    if (linea.find('|') != std::string::npos) return true;
    if (linea.find('^') != std::string::npos) return true;
    return false;
}

static bool linea_consola_es_aviso(const std::string& linea) {
    std::string up = linea;
    for (char& c : up) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }
    return (up.find("AVISO") != std::string::npos || up.find("WARN") != std::string::npos);
}

static bool linea_consola_es_vm(const std::string& linea) {
    return (linea.find("[VM]") != std::string::npos);
}

static bool linea_consola_visible(const std::string& linea, int filtro_consola) {
    // ALDA: Filtro simple por categoria para no complicar la lectura en una IDE escolar.
    if (filtro_consola == 1) {
        return linea_consola_es_error(linea) || linea_consola_es_contexto_error(linea);
    }
    if (filtro_consola == 2) {
        return linea_consola_es_vm(linea);
    }
    return true;
}



static std::string construir_texto_consola_filtrada(const std::vector<std::string>& lineas,
                                                    int filtro_consola) {
    std::ostringstream salida;
    for (const std::string& l : lineas) {
        if (!linea_consola_visible(l, filtro_consola)) {
            continue;
        }
        salida << l;
        // AARON: Si alguna linea no trae salto final, lo agregamos para mantener formato consistente.
        if (l.empty() || l.back() != '\n') {
            salida << '\n';
        }
    }
    return salida.str();
}

static bool seleccionar_ruta_guardado_log(std::string& ruta_salida) {
    // AVILA: Dialogo dedicado para exportar texto de consola en formato .txt.
    char archivo[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = archivo;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Logs de texto (*.txt)\0*.txt\0Todos (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "txt";
    ofn.lpstrTitle = "Exportar log de consola";

    if (!GetSaveFileNameA(&ofn)) {
        return false;
    }

    ruta_salida = archivo;
    return true;
}

static std::string ruta_entre_comillas(const std::string& ruta) {
    std::string q = "\"";
    q += ruta;
    q += "\"";
    return q;
}

static std::string normalizar_ruta_para_cmd(const std::filesystem::path& p) {
    std::string s = p.string();
    // LUIS: cmd.exe puede fallar con prefijos extendidos \\?\, por eso los removemos.
    const std::string prefijo_local = "\\\\?\\";
    const std::string prefijo_unc = "\\\\?\\UNC\\";
    if (s.rfind(prefijo_unc, 0) == 0) {
        // Convierte \\?\UNC\server\share\... en \\server\share\...
        s = "\\\\" + s.substr(prefijo_unc.size());
    } else if (s.rfind(prefijo_local, 0) == 0) {
        // Convierte \\?\C:\... en C:\...
        s = s.substr(prefijo_local.size());
    }
    return s;
}

static std::filesystem::path obtener_directorio_ejecutable() {
    char ruta_modulo[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(nullptr, ruta_modulo, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(ruta_modulo).parent_path();
}

static bool seleccionar_carpeta(std::string& ruta_salida) {
    // LUIS: Este bloque abre el dialogo nativo de Windows para escoger una carpeta valida.
    // LUIS: Si el usuario cancela, regresamos false para que la UI no cambie estado por error.
    BROWSEINFOA bi{};
    bi.lpszTitle = "Selecciona carpeta de trabajo";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (!id) {
        return false;
    }

    char path[MAX_PATH] = {0};
    bool ok = SHGetPathFromIDListA(id, path) == TRUE;

    IMalloc* imalloc = nullptr;
    if (SUCCEEDED(SHGetMalloc(&imalloc)) && imalloc) {
        imalloc->Free(id);
        imalloc->Release();
    }

    if (!ok) {
        return false;
    }

    ruta_salida = path;
    return true;
}

static bool escribir_archivo_texto(const std::string& ruta, const std::string& contenido) {
    // ALDA: Guardamos en binario para no alterar saltos de linea ni caracteres del editor.
    std::ofstream out(ruta, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out << contenido;
    return out.good();
}

static bool leer_archivo_texto(const std::string& ruta, std::string& contenido) {
    // AARON: Leemos todo el archivo a memoria para cargarlo completo en el editor multilinea.
    std::ifstream in(ruta, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    contenido = ss.str();
    return in.good() || in.eof();
}

static bool seleccionar_archivo_scara(std::string& ruta_salida) {
    // AVILA: Este selector limita por defecto a .scara para mantener el flujo de la materia.
    char archivo[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = archivo;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Archivos SCARA (*.scara)\0*.scara\0Todos (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = "Abrir archivo SCARA";

    if (!GetOpenFileNameA(&ofn)) {
        return false;
    }

    ruta_salida = archivo;
    return true;
}

static bool seleccionar_ruta_guardado_scara(std::string& ruta_salida) {
    // AVILA: Este dialogo permite elegir destino de guardado cuando aun no hay archivo activo.
    char archivo[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = archivo;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Archivos SCARA (*.scara)\0*.scara\0Todos (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "scara";
    ofn.lpstrTitle = "Guardar archivo SCARA";

    if (!GetSaveFileNameA(&ofn)) {
        return false;
    }

    ruta_salida = archivo;
    return true;
}

struct EstadoEjecucion {
    std::atomic<bool> en_curso{false};
    std::atomic<bool> finalizado{false};
    std::atomic<int> codigo_salida{0};
    std::atomic<bool> solicitud_detener{false};
    std::mutex mutex_lineas;
    std::deque<std::string> lineas_pendientes;
    std::thread hilo;
};

static void anexar_linea_pendiente(EstadoEjecucion& estado, const std::string& linea) {
    // LUIS: Como este metodo puede ser llamado desde el hilo de ejecucion, protegemos la cola.
    std::lock_guard<std::mutex> lock(estado.mutex_lineas);
    estado.lineas_pendientes.push_back(linea);
}

static void ejecutar_comando_en_segundo_plano(const std::string& cmd, EstadoEjecucion* estado) {
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        anexar_linea_pendiente(*estado, "[IDE] ERROR: no se pudo lanzar el proceso.\n");
        estado->codigo_salida = -1;
        estado->en_curso = false;
        estado->finalizado = true;
        return;
    }

    char buffer[1024];
    // AARON: Mientras haya salida disponible del proceso, la vamos publicando en tiempo real.
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        anexar_linea_pendiente(*estado, std::string(buffer));
    }

    estado->codigo_salida = _pclose(pipe);
    if (estado->solicitud_detener.load()) {
        anexar_linea_pendiente(*estado, "[IDE] Ejecucion detenida por el usuario.\n");
    }
    estado->en_curso = false;
    estado->finalizado = true;
}

static void drenar_lineas_pendientes(EstadoEjecucion& estado,
                                     std::vector<std::string>& consola,
                                     size_t max_lineas_consola) {
    std::lock_guard<std::mutex> lock(estado.mutex_lineas);
    // ALDA: Drenamos todo lo pendiente en bloque para reducir costo de lock y evitar parpadeos.
    while (!estado.lineas_pendientes.empty()) {
        consola.push_back(estado.lineas_pendientes.front());
        estado.lineas_pendientes.pop_front();
    }
    limitar_consola(consola, max_lineas_consola);
}

static void limitar_consola(std::vector<std::string>& consola, size_t max_lineas) {
    if (consola.size() <= max_lineas) {
        return;
    }
    const size_t excedente = consola.size() - max_lineas;
    consola.erase(consola.begin(), consola.begin() + static_cast<std::ptrdiff_t>(excedente));
}

static bool abrir_archivo_en_editor(const std::string& nueva_ruta,
                                    std::string& ruta_archivo_activo,
                                    std::string& codigo_editor,
                                    bool& archivo_modificado,
                                    std::string& carpeta_trabajo,
                                    std::vector<std::string>& lineas_consola,
                                    size_t max_lineas_consola) {
    std::string contenido;
    if (!leer_archivo_texto(nueva_ruta, contenido)) {
        lineas_consola.push_back("[IDE] ERROR: no se pudo leer el archivo seleccionado.\n");
        limitar_consola(lineas_consola, max_lineas_consola);
        return false;
    }

    ruta_archivo_activo = nueva_ruta;
    codigo_editor = contenido;
    archivo_modificado = false;
    std::filesystem::path p = std::filesystem::path(ruta_archivo_activo);
    carpeta_trabajo = p.parent_path().string();
    lineas_consola.push_back("[IDE] Archivo abierto: " + ruta_archivo_activo + "\n");
    limitar_consola(lineas_consola, max_lineas_consola);
    return true;
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* ventana = SDL_CreateWindow(
        "SCARA",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1080,
        700,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED
    );
    if (!ventana) {
        std::printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        ventana,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        std::printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(ventana);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsLight();

    ImGui_ImplSDL2_InitForSDLRenderer(ventana, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool corriendo = true;

    std::string codigo_editor = "# Programa SCARA\nPROGRAM demo\n  POINT p1 = (100, 120, 20)\n  MOVE p1\nEND\n";
    std::string carpeta_trabajo;
    std::string ruta_archivo_activo;
    bool archivo_modificado = false;
    bool panel_derecho_abierto = true;
    bool auto_guardar_antes_ejecutar = true;
    float proporcion_altura_consola = 0.28f;
    int filtro_consola = 0; // 0=Todo, 1=Errores, 2=VM
    bool enfocar_consola_rapido = false;
    bool solicitar_confirmacion_cierre = false;
    bool cerrar_al_terminar = false;
    bool cfg_cargada_desde_archivo = false;
    int estado_lineas_visibles = 0;
    int estado_errores_visibles = 0;
    int estado_avisos_visibles = 0;
    int estado_vm_visibles = 0;
    bool hubo_ejecucion = false;
    int ultimo_codigo_ejecucion = 0;
    std::vector<std::string> lineas_consola = {
        "[IDE] Lista. Selecciona carpeta y pulsa Ejecutar.\n"
    };
    EstadoEjecucion estado_ejecucion;

    const size_t max_lineas_consola = 1200;
    std::filesystem::path ruta_scara_exe = obtener_directorio_ejecutable() / ".." / "programasCOMPI" / "scara.exe";
    std::error_code ec_scara;
    ruta_scara_exe = std::filesystem::weakly_canonical(ruta_scara_exe, ec_scara);
    if (ec_scara) {
        ruta_scara_exe = obtener_directorio_ejecutable() / ".." / "programasCOMPI" / "scara.exe";
    }

    // AARON: Restauramos preferencias previas para retomar el contexto sin reconfigurar cada sesion.
    cfg_cargada_desde_archivo = cargar_cfg_ide_basica(k_archivo_cfg_ide_basica,
                                                       panel_derecho_abierto,
                                                       auto_guardar_antes_ejecutar,
                                                       proporcion_altura_consola,
                                                       filtro_consola);

    // AVILA: Loop principal de la app; cada vuelta procesa eventos, dibuja UI y sincroniza consola.
    while (corriendo) {
        SDL_Event event;
        // LUIS: Este bucle consume todos los eventos pendientes para mantener la ventana responsiva.
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                // LUIS: Si hay cambios sin guardar, no cerramos de inmediato; primero pedimos confirmacion.
                if (archivo_modificado) {
                    solicitar_confirmacion_cierre = true;
                } else {
                    corriendo = false;
                }
            }
            // AARON: Si el usuario cierra la ventana principal, cerramos el loop de forma ordenada.
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(ventana)) {
                if (archivo_modificado) {
                    solicitar_confirmacion_cierre = true;
                } else {
                    corriendo = false;
                }
            }
        }

        drenar_lineas_pendientes(estado_ejecucion, lineas_consola, max_lineas_consola);
        if (estado_ejecucion.finalizado.exchange(false)) {
            if (estado_ejecucion.hilo.joinable()) {
                estado_ejecucion.hilo.join();
            }
            ultimo_codigo_ejecucion = estado_ejecucion.codigo_salida.load();
            hubo_ejecucion = true;
            lineas_consola.push_back("[IDE] Proceso finalizado. Codigo=" + std::to_string(ultimo_codigo_ejecucion) + "\n");
            if (ultimo_codigo_ejecucion != 0) {
                lineas_consola.push_back("[IDE] ERROR: la ejecucion termino con fallo (codigo no cero).\n");
            } else {
                lineas_consola.push_back("[IDE] OK: la ejecucion termino correctamente.\n");
            }
            limitar_consola(lineas_consola, max_lineas_consola);
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("IDE_BASICA_SCARA", nullptr,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoTitleBar);

        if (ImGui::Button("Seleccionar carpeta")) {
            std::string nueva;
            if (seleccionar_carpeta(nueva)) {
                carpeta_trabajo = nueva;
                lineas_consola.push_back("[IDE] Carpeta seleccionada: " + carpeta_trabajo + "\n");
            } else {
                // ALDA: Si cancela el dialogo, lo registramos en consola para trazabilidad escolar.
                lineas_consola.push_back("[IDE] Seleccion de carpeta cancelada.\n");
            }
            limitar_consola(lineas_consola, max_lineas_consola);
        }
        ImGui::SameLine();
        if (ImGui::Button("Abrir .scara")) {
            std::string nueva_ruta;
            if (seleccionar_archivo_scara(nueva_ruta)) {
                abrir_archivo_en_editor(nueva_ruta,
                                        ruta_archivo_activo,
                                        codigo_editor,
                                        archivo_modificado,
                                        carpeta_trabajo,
                                        lineas_consola,
                                        max_lineas_consola);
            } else {
                lineas_consola.push_back("[IDE] Apertura de archivo cancelada.\n");
                limitar_consola(lineas_consola, max_lineas_consola);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Guardar")) {
            std::filesystem::path ruta_guardado;
            if (!ruta_archivo_activo.empty()) {
                ruta_guardado = std::filesystem::path(ruta_archivo_activo);
            } else {
                std::string nueva_ruta;
                if (seleccionar_ruta_guardado_scara(nueva_ruta)) {
                    ruta_guardado = std::filesystem::path(nueva_ruta);
                }
            }

            // AARON: Si el usuario cancela el guardado, dejamos todo intacto sin marcar error.
            if (ruta_guardado.empty()) {
                lineas_consola.push_back("[IDE] Guardado cancelado.\n");
            } else if (!escribir_archivo_texto(ruta_guardado.string(), codigo_editor)) {
                lineas_consola.push_back("[IDE] ERROR: no se pudo guardar el archivo.\n");
            } else {
                ruta_archivo_activo = ruta_guardado.string();
                carpeta_trabajo = ruta_guardado.parent_path().string();
                archivo_modificado = false;
                lineas_consola.push_back("[IDE] Archivo guardado: " + ruta_archivo_activo + "\n");
            }
            limitar_consola(lineas_consola, max_lineas_consola);
        }
        ImGui::SameLine();
        if (ImGui::Button("Guardar como")) {
            std::string nueva_ruta;
            if (!seleccionar_ruta_guardado_scara(nueva_ruta)) {
                lineas_consola.push_back("[IDE] Guardar como cancelado.\n");
            } else if (!escribir_archivo_texto(nueva_ruta, codigo_editor)) {
                lineas_consola.push_back("[IDE] ERROR: no se pudo guardar el archivo.\n");
            } else {
                ruta_archivo_activo = nueva_ruta;
                std::filesystem::path p = std::filesystem::path(ruta_archivo_activo);
                carpeta_trabajo = p.parent_path().string();
                archivo_modificado = false;
                lineas_consola.push_back("[IDE] Archivo guardado como: " + ruta_archivo_activo + "\n");
            }
            limitar_consola(lineas_consola, max_lineas_consola);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Panel derecho", &panel_derecho_abierto);
        ImGui::SameLine();
        // AARON: Este switch deja elegir entre flujo seguro (auto-guardar) o flujo manual (ejecutar guardado previo).
        ImGui::Checkbox("Auto-guardar al ejecutar", &auto_guardar_antes_ejecutar);
        ImGui::SameLine();
        // LUIS: Mostramos 0-100 para UX simple; internamente se mapea al rango seguro de layout [0.18, 0.70].
        const float min_altura_consola = 0.18f;
        const float max_altura_consola = 0.70f;
        int porcentaje_consola = static_cast<int>(
            ((proporcion_altura_consola - min_altura_consola) / (max_altura_consola - min_altura_consola)) * 100.0f + 0.5f);
        if (porcentaje_consola < 0) porcentaje_consola = 0;
        if (porcentaje_consola > 100) porcentaje_consola = 100;
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::SliderInt("Consola %", &porcentaje_consola, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            proporcion_altura_consola = min_altura_consola +
                                        (static_cast<float>(porcentaje_consola) / 100.0f) *
                                        (max_altura_consola - min_altura_consola);
        }
        ImGui::SameLine();
        if (ImGui::Button("Restaurar defaults")) {
            // AVILA: Reset rapido de preferencias para volver a un estado conocido durante clase/pruebas.
            panel_derecho_abierto = true;
            auto_guardar_antes_ejecutar = true;
            proporcion_altura_consola = 0.28f;
            filtro_consola = 0;
            lineas_consola.push_back("[IDE] Preferencias restauradas a defaults.\n");
            limitar_consola(lineas_consola, max_lineas_consola);
        }
        ImGui::SameLine();

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            // ALDA: Atajo escolar clasico para guardar rapido sin mover la mano al mouse.
            std::filesystem::path ruta_guardado;
            if (!ruta_archivo_activo.empty()) {
                ruta_guardado = std::filesystem::path(ruta_archivo_activo);
            } else {
                std::string nueva_ruta;
                if (seleccionar_ruta_guardado_scara(nueva_ruta)) {
                    ruta_guardado = std::filesystem::path(nueva_ruta);
                }
            }

            if (ruta_guardado.empty()) {
                lineas_consola.push_back("[IDE] Guardado cancelado.\n");
            } else if (!escribir_archivo_texto(ruta_guardado.string(), codigo_editor)) {
                lineas_consola.push_back("[IDE] ERROR: no se pudo guardar el archivo.\n");
            } else {
                ruta_archivo_activo = ruta_guardado.string();
                carpeta_trabajo = ruta_guardado.parent_path().string();
                archivo_modificado = false;
                lineas_consola.push_back("[IDE] Archivo guardado (Ctrl+S): " + ruta_archivo_activo + "\n");
            }
            limitar_consola(lineas_consola, max_lineas_consola);
        }

        if (ImGui::Button("Ejecutar")) {
            if (estado_ejecucion.en_curso.load()) {
                lineas_consola.push_back("[IDE] Ya hay una ejecucion en curso. Espera a que termine.\n");
                limitar_consola(lineas_consola, max_lineas_consola);
            } else {
                std::filesystem::path ruta_archivo;
                if (!ruta_archivo_activo.empty()) {
                    ruta_archivo = std::filesystem::path(ruta_archivo_activo);
                } else if (!carpeta_trabajo.empty()) {
                    ruta_archivo = std::filesystem::path(carpeta_trabajo) / "programa_activo.scara";
                }

                // AVILA: Si no tenemos objetivo de guardado, detenemos la ejecucion para evitar null paths.
                if (ruta_archivo.empty()) {
                    lineas_consola.push_back("[IDE] ERROR: primero selecciona carpeta o abre un .scara.\n");
                } else {
                    bool puede_ejecutar = true;
                    if (auto_guardar_antes_ejecutar) {
                        // LUIS: Modo recomendado para clase: ejecuta exactamente lo que esta en el editor.
                        const bool ok_guardado = escribir_archivo_texto(ruta_archivo.string(), codigo_editor);
                        if (!ok_guardado) {
                            lineas_consola.push_back("[IDE] ERROR: no se pudo guardar el archivo activo.\n");
                            puede_ejecutar = false;
                        } else {
                            ruta_archivo_activo = ruta_archivo.string();
                            carpeta_trabajo = ruta_archivo.parent_path().string();
                            archivo_modificado = false;
                        }
                    } else {
                        // ALDA: En modo manual, no tocamos disco; solo permitimos ejecutar una version ya guardada.
                        if (!std::filesystem::exists(ruta_archivo)) {
                            lineas_consola.push_back("[IDE] ERROR: el archivo aun no existe en disco. Guarda primero.\n");
                            puede_ejecutar = false;
                        } else if (archivo_modificado) {
                            lineas_consola.push_back("[IDE] Aviso: ejecutando ultima version guardada (hay cambios sin guardar).\n");
                        }
                    }

                    if (puede_ejecutar) {
                        if (!std::filesystem::exists(ruta_scara_exe)) {
                            // AARON: Usamos ruta absoluta para evitar fallos por directorio de arranque distinto.
                            lineas_consola.push_back("[IDE] ERROR: no se encontro scara.exe en: " + ruta_scara_exe.string() + "\n");
                            lineas_consola.push_back("[IDE] Verifica que exista en ..\\programasCOMPI\\scara.exe\n");
                            limitar_consola(lineas_consola, max_lineas_consola);
                            puede_ejecutar = false;
                        }
                    }

                    if (puede_ejecutar) {
                        lineas_consola.push_back("[IDE] Ejecutando scara.exe con visualizacion...\n");
                        std::string ruta_exe_cmd = normalizar_ruta_para_cmd(ruta_scara_exe);
                        std::string ruta_src_cmd = normalizar_ruta_para_cmd(ruta_archivo);
                        // AVILA: _popen usa cmd.exe internamente; esta forma evita errores de parseo con comillas.
                        std::string cmd = "cmd /d /s /c \"\"" + ruta_exe_cmd + "\" --vis-keep-open \"" +
                                          ruta_src_cmd + "\" 2>&1\"";
                        lineas_consola.push_back("[IDE] CMD: " + cmd + "\n");
                        if (estado_ejecucion.hilo.joinable()) {
                            estado_ejecucion.hilo.join();
                        }
                        estado_ejecucion.codigo_salida = 0;
                        estado_ejecucion.finalizado = false;
                        estado_ejecucion.en_curso = true;
                        estado_ejecucion.solicitud_detener = false;
                        estado_ejecucion.hilo = std::thread(ejecutar_comando_en_segundo_plano, cmd, &estado_ejecucion);
                    }
                }
                limitar_consola(lineas_consola, max_lineas_consola);
            }
        }
        ImGui::SameLine();
        bool puede_detener = estado_ejecucion.en_curso.load();
        if (!puede_detener) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Detener")) {
            // AVILA: Como usamos _popen, detenemos por nombre de proceso para terminar la corrida activa.
            estado_ejecucion.solicitud_detener = true;
            std::system("taskkill /F /IM scara.exe >nul 2>&1");
            lineas_consola.push_back("[IDE] Solicitud de detencion enviada a scara.exe\n");
            limitar_consola(lineas_consola, max_lineas_consola);
        }
        if (!puede_detener) {
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        if (estado_ejecucion.en_curso.load()) {
            ImGui::TextColored(ImVec4(0.88f, 0.45f, 0.06f, 1.0f), "Estado: Ejecutando en segundo plano...");
        } else {
            ImGui::TextColored(ImVec4(0.10f, 0.50f, 0.18f, 1.0f), "Estado: Listo");
        }

        ImGui::Separator();
        const char* etiqueta_filtro = (filtro_consola == 1) ? "Errores" : ((filtro_consola == 2) ? "VM" : "Todo");
        const char* etiqueta_cfg = cfg_cargada_desde_archivo ? "CFG cargada" : "CFG default";
        std::string etiqueta_archivo = ruta_archivo_activo.empty() ? "(temporal)" : ruta_archivo_activo;
        ImVec4 color_estado = ImVec4(0.10f, 0.50f, 0.18f, 1.0f);
        const char* etiqueta_estado = "LISTO";
        if (estado_ejecucion.en_curso.load()) {
            color_estado = ImVec4(0.88f, 0.45f, 0.06f, 1.0f);
            etiqueta_estado = "EJECUTANDO";
        } else if (hubo_ejecucion && ultimo_codigo_ejecucion != 0) {
            color_estado = ImVec4(0.82f, 0.18f, 0.16f, 1.0f);
            etiqueta_estado = "ERROR DE EJECUCION";
        } else if (estado_errores_visibles > 0) {
            color_estado = ImVec4(0.82f, 0.18f, 0.16f, 1.0f);
            etiqueta_estado = "CON ERRORES";
        } else if (estado_avisos_visibles > 0) {
            color_estado = ImVec4(0.80f, 0.52f, 0.07f, 1.0f);
            etiqueta_estado = "CON AVISOS";
        }
        // AARON: Barra de estado compacta para mostrar contexto de trabajo sin abrir paneles extra.
        ImGui::PushStyleColor(ImGuiCol_Text, color_estado);
        ImGui::Text("Archivo: %s | Filtro: %s | Visibles: %d | %s",
                etiqueta_archivo.c_str(),
                etiqueta_filtro,
                estado_lineas_visibles,
                etiqueta_cfg);
        ImGui::PopStyleColor();
        ImGui::Text("Estado: %s |", etiqueta_estado);
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("Err: ") + std::to_string(estado_errores_visibles)).c_str())) {
            // LUIS: Atajo visual para saltar directo al filtro de errores desde la barra de estado.
            filtro_consola = 1;
            enfocar_consola_rapido = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("Avisos: ") + std::to_string(estado_avisos_visibles)).c_str())) {
            filtro_consola = 0;
            enfocar_consola_rapido = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("VM: ") + std::to_string(estado_vm_visibles)).c_str())) {
            filtro_consola = 2;
            enfocar_consola_rapido = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Todo")) {
            filtro_consola = 0;
            enfocar_consola_rapido = true;
        }
        ImGui::Separator();

        float ancho_total = ImGui::GetContentRegionAvail().x;
        float alto_total = ImGui::GetContentRegionAvail().y;
        float alto_consola = alto_total * proporcion_altura_consola;
        if (alto_consola < 160.0f) alto_consola = 160.0f;

        float ancho_panel_derecho = panel_derecho_abierto ? 300.0f : 0.0f;
        float ancho_editor = ancho_total - ancho_panel_derecho;
        if (ancho_editor < 280.0f) {
            ancho_editor = 280.0f;
        }

        ImGui::BeginChild("ZonaSuperior", ImVec2(0, alto_total - alto_consola), false);

        ImGui::BeginChild("EditorCentral", ImVec2(ancho_editor, 0), true);
        ImGui::TextUnformatted("Editor");
        if (!ruta_archivo_activo.empty()) {
            ImGui::TextWrapped("Archivo activo: %s", ruta_archivo_activo.c_str());
        } else {
            ImGui::TextUnformatted("Archivo activo: (temporal)");
        }
        ImGui::SameLine();
        if (archivo_modificado) {
            ImGui::TextColored(ImVec4(0.88f, 0.45f, 0.06f, 1.0f), "* Modificado");
        } else {
            ImGui::TextColored(ImVec4(0.10f, 0.50f, 0.18f, 1.0f), "Guardado");
        }
        ImGui::Separator();
        // LUIS: Si InputTextMultiline devuelve true, significa que el usuario cambio contenido.
        bool hubo_cambio = ImGui::InputTextMultiline("##editor_scara", &codigo_editor,
                                                     ImVec2(-1.0f, -1.0f),
                                                     ImGuiInputTextFlags_AllowTabInput);
        if (hubo_cambio) {
            archivo_modificado = true;
        }
        ImGui::EndChild();

        if (panel_derecho_abierto) {
            ImGui::SameLine();
            ImGui::BeginChild("PanelDerecho", ImVec2(0, 0), true);
            if (ImGui::CollapsingHeader("Carpeta de trabajo", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Abrir .scara##panel")) {
                    std::string nueva_ruta;
                    if (seleccionar_archivo_scara(nueva_ruta)) {
                        abrir_archivo_en_editor(nueva_ruta,
                                                ruta_archivo_activo,
                                                codigo_editor,
                                                archivo_modificado,
                                                carpeta_trabajo,
                                                lineas_consola,
                                                max_lineas_consola);
                    }
                }
                if (ImGui::Button("Seleccionar carpeta##panel")) {
                    std::string nueva;
                    if (seleccionar_carpeta(nueva)) {
                        carpeta_trabajo = nueva;
                        lineas_consola.push_back("[IDE] Carpeta seleccionada: " + carpeta_trabajo + "\n");
                        limitar_consola(lineas_consola, max_lineas_consola);
                    }
                }
                ImGui::Separator();
                if (carpeta_trabajo.empty()) {
                    ImGui::TextUnformatted("(sin carpeta)");
                } else {
                    ImGui::TextWrapped("%s", carpeta_trabajo.c_str());
                }

                ImGui::Separator();
                if (ImGui::CollapsingHeader("Archivos de la carpeta", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (carpeta_trabajo.empty()) {
                        ImGui::TextUnformatted("Selecciona una carpeta para ver archivos.");
                    } else {
                        std::vector<std::pair<std::string, std::string>> archivos;
                        std::error_code ec_dir;
                        for (const auto& entry : std::filesystem::directory_iterator(carpeta_trabajo, ec_dir)) {
                            if (ec_dir) break;
                            if (!entry.is_regular_file()) continue;
                            std::string nombre = entry.path().filename().string();
                            std::string ruta_full = entry.path().string();
                            archivos.push_back({nombre, ruta_full});
                        }

                        std::sort(archivos.begin(), archivos.end(),
                                  [](const auto& a, const auto& b) { return comparar_natural(a.first, b.first) < 0; });

                        // ALDA: Mostramos listado simple y abrimos con clic para acelerar flujo escolar.
                        if (archivos.empty()) {
                            ImGui::TextUnformatted("(sin archivos visibles)");
                        } else {
                            ImGui::BeginChild("ListaArchivosCarpeta", ImVec2(0, 160), true);
                            for (const auto& item : archivos) {
                                const std::string& nombre = item.first;
                                const std::string& ruta_full = item.second;
                                bool es_scara = (std::filesystem::path(nombre).extension().string() == ".scara");
                                if (es_scara) {
                                    if (ImGui::Selectable(("[SCARA] " + nombre).c_str(), false)) {
                                        abrir_archivo_en_editor(ruta_full,
                                                                ruta_archivo_activo,
                                                                codigo_editor,
                                                                archivo_modificado,
                                                                carpeta_trabajo,
                                                                lineas_consola,
                                                                max_lineas_consola);
                                    }
                                } else {
                                    ImGui::TextUnformatted(nombre.c_str());
                                }
                            }
                            ImGui::EndChild();
                        }
                    }
                }
            }
            ImGui::EndChild();
        }

        ImGui::EndChild();

        ImGui::Separator();

        ImGui::BeginChild("ConsolaInferior", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        if (enfocar_consola_rapido) {
            // AARON: Al usar accesos rapidos de estado, movemos foco a consola para continuar inspeccion.
            ImGui::SetWindowFocus();
            enfocar_consola_rapido = false;
        }
        ImGui::TextUnformatted("Consola");
        ImGui::Separator();
        if (ImGui::Button("Limpiar consola")) {
            lineas_consola.clear();
            lineas_consola.push_back("[IDE] Consola limpiada.\n");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Filtro:");
        ImGui::SameLine();
        // LUIS: Estos botones cambian la vista sin borrar historial, solo afectan lo que se muestra.
        if (ImGui::RadioButton("Todo", filtro_consola == 0)) {
            filtro_consola = 0;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Errores", filtro_consola == 1)) {
            filtro_consola = 1;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("VM", filtro_consola == 2)) {
            filtro_consola = 2;
        }
        ImGui::SameLine();
        if (ImGui::Button("Copiar consola")) {
            // LUIS: Copiamos solo lineas visibles para que el reporte respete el filtro activo.
            std::string texto = construir_texto_consola_filtrada(lineas_consola, filtro_consola);
            if (texto.empty()) {
                lineas_consola.push_back("[IDE] Aviso: no hay lineas visibles para copiar.\n");
            } else {
                if (SDL_SetClipboardText(texto.c_str()) == 0) {
                    lineas_consola.push_back("[IDE] Consola copiada al portapapeles.\n");
                } else {
                    lineas_consola.push_back("[IDE] ERROR: no se pudo copiar al portapapeles.\n");
                }
            }
            limitar_consola(lineas_consola, max_lineas_consola);
        }
        ImGui::SameLine();
        if (ImGui::Button("Exportar .txt")) {
            std::string ruta_log;
            if (!seleccionar_ruta_guardado_log(ruta_log)) {
                lineas_consola.push_back("[IDE] Exportacion cancelada.\n");
            } else {
                std::string texto = construir_texto_consola_filtrada(lineas_consola, filtro_consola);
                // ALDA: Exportamos segun filtro activo para generar evidencia exacta de lo observado.
                if (!escribir_archivo_texto(ruta_log, texto)) {
                    lineas_consola.push_back("[IDE] ERROR: no se pudo exportar el log.\n");
                } else {
                    lineas_consola.push_back("[IDE] Log exportado: " + ruta_log + "\n");
                }
            }
            limitar_consola(lineas_consola, max_lineas_consola);
        }
        ImGui::SameLine();
        ImGui::Text("Lineas: %d", static_cast<int>(lineas_consola.size()));
        ImGui::Separator();

        int lineas_visibles = 0;
        int errores_visibles = 0;
        int avisos_visibles = 0;
        int vm_visibles = 0;
        for (int i = 0; i < static_cast<int>(lineas_consola.size()); ++i) {
            const std::string& l = lineas_consola[i];
            // AARON: Recorremos todas las lineas y renderizamos solo las que pasan el filtro activo.
            if (!linea_consola_visible(l, filtro_consola)) {
                continue;
            }

            lineas_visibles++;
            if (linea_consola_es_error(l)) {
                errores_visibles++;
            }
            if (linea_consola_es_aviso(l)) {
                avisos_visibles++;
            }
            if (linea_consola_es_vm(l)) {
                vm_visibles++;
            }

            std::string texto = l;
            while (!texto.empty() && (texto.back() == '\n' || texto.back() == '\r')) {
                texto.pop_back();
            }
            ImGui::TextUnformatted(texto.c_str());
        }
        estado_lineas_visibles = lineas_visibles;
        estado_errores_visibles = errores_visibles;
        estado_avisos_visibles = avisos_visibles;
        estado_vm_visibles = vm_visibles;

        ImGui::Separator();
        ImGui::Text("Visibles: %d", lineas_visibles);

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        if (solicitar_confirmacion_cierre) {
            // AVILA: Abrimos popup modal para que el usuario decida si quiere perder cambios no guardados.
            ImGui::OpenPopup("Confirmar cierre");
            solicitar_confirmacion_cierre = false;
        }

        bool popup_abierto = true;
        if (ImGui::BeginPopupModal("Confirmar cierre", &popup_abierto, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Hay cambios sin guardar.\n\nDeseas salir sin guardar?");
            ImGui::Separator();

            // ALDA: Estas tres rutas dejan explicito cada caso: guardar y salir, salir sin guardar, cancelar.
            if (ImGui::Button("Guardar y salir", ImVec2(140, 0))) {
                std::filesystem::path ruta_guardado;
                if (!ruta_archivo_activo.empty()) {
                    ruta_guardado = std::filesystem::path(ruta_archivo_activo);
                } else {
                    std::string nueva_ruta;
                    if (seleccionar_ruta_guardado_scara(nueva_ruta)) {
                        ruta_guardado = std::filesystem::path(nueva_ruta);
                    }
                }

                if (ruta_guardado.empty()) {
                    lineas_consola.push_back("[IDE] Guardado cancelado. Cierre cancelado.\n");
                    limitar_consola(lineas_consola, max_lineas_consola);
                    ImGui::CloseCurrentPopup();
                } else if (!escribir_archivo_texto(ruta_guardado.string(), codigo_editor)) {
                    lineas_consola.push_back("[IDE] ERROR: no se pudo guardar. Cierre cancelado.\n");
                    limitar_consola(lineas_consola, max_lineas_consola);
                    ImGui::CloseCurrentPopup();
                } else {
                    ruta_archivo_activo = ruta_guardado.string();
                    carpeta_trabajo = ruta_guardado.parent_path().string();
                    archivo_modificado = false;
                    cerrar_al_terminar = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Salir sin guardar", ImVec2(140, 0))) {
                archivo_modificado = false;
                cerrar_al_terminar = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar", ImVec2(100, 0))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (cerrar_al_terminar) {
            corriendo = false;
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 245, 246, 249, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    guardar_cfg_ide_basica(k_archivo_cfg_ide_basica,
                           panel_derecho_abierto,
                           auto_guardar_antes_ejecutar,
                           proporcion_altura_consola,
                           filtro_consola);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // AARON: Antes de salir, esperamos el hilo si sigue activo para evitar recursos colgados.
    if (estado_ejecucion.hilo.joinable()) {
        estado_ejecucion.hilo.join();
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(ventana);
    SDL_Quit();
    return 0;
}
