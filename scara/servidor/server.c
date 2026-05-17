/*
 * server.c — Servidor HTTP local para el compilador SCARA
 * Winsock puro, sin dependencias externas (no necesita mongoose).
 *
 * Compilar (desde la carpeta servidor/ en MSYS2):
 *   gcc -O2 server.c -o server.exe -lws2_32 -lshell32
 *
 * Endpoints:
 *   POST /compile   — recibe {"code":"..."}, compila con scara.exe,
 *                     devuelve {"ok":bool, "output":"..."}
 *   GET  /run       — lanza temp/input.exe con ShellExecute
 *   GET  /          — sirve ../frontend/index.html
 *
 * Ejecutar server.exe DESDE la carpeta servidor/ para que las rutas
 * relativas apunten correctamente.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>   /* socket, bind, listen, accept, recv, send */
#include <windows.h>    /* CreateDirectoryA, ShellExecuteA           */
#include <shellapi.h>   /* ShellExecuteA                             */

#pragma comment(lib, "ws2_32.lib")

/* Rutas absolutas — se calculan en main() a partir de la ubicacion
 * real del .exe, usando GetModuleFileNameA + PathCombine manual.
 * Esto evita cualquier problema con working directory o espacios. */
static char RUTA_SCARA   [MAX_PATH];
static char RUTA_TEMP    [MAX_PATH];
static char RUTA_SCARA_IN[MAX_PATH];
static char RUTA_EXE_OUT [MAX_PATH];
static char RUTA_INDEX   [MAX_PATH];
#define PUERTO 8080

/* Construye una ruta absoluta: base_dir + "\\" + sufijo → dst */
static void construir_ruta(char *dst, const char *base, const char *sufijo) {
    snprintf(dst, MAX_PATH, "%s\\%s", base, sufijo);
}

/* ════════════════════════════════════════════════════════════════
 * 1. Utilidades de string
 * ════════════════════════════════════════════════════════════════ */

/*
 * Escapa caracteres especiales para incrustar texto en JSON.
 *   \  →  \\     "  →  \"     \n  →  \n literal (dos chars)
 */
static void json_escape(const char *src, char *dst, int max) {
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < max - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if      (c == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (c == '"')  { dst[j++] = '\\'; dst[j++] = '"';  }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n';  }
        else if (c == '\r') { dst[j++] = '\\'; dst[j++] = 'r';  }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't';  }
        else if (c < 0x20)  { /* otros de control: ignorar */     }
        else                { dst[j++] = c;                       }
    }
    dst[j] = '\0';
}

/*
 * Extrae el valor del campo "code" de un JSON simple.
 * Maneja escapes  \"  \\  \n  \r  \t  dentro del valor.
 * Devuelve 1 si lo encontro, 0 si no.
 */
static int json_leer_code(const char *json, char *dst, int max) {
    const char *p = strstr(json, "\"code\"");
    if (!p) return 0;
    p += 6;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;  /* entrar al contenido */

    int j = 0;
    while (*p != '\0' && j < max - 1) {
        if (*p == '\\') {
            p++;
            if      (*p == 'n')  { dst[j++] = '\n'; }
            else if (*p == 'r')  { dst[j++] = '\r'; }
            else if (*p == 't')  { dst[j++] = '\t'; }
            else if (*p == '"')  { dst[j++] = '"';  }
            else if (*p == '\\') { dst[j++] = '\\'; }
            else                 { dst[j++] = *p;   }
            p++;
        } else if (*p == '"') {
            break;
        } else {
            dst[j++] = *p++;
        }
    }
    dst[j] = '\0';
    return 1;
}

/* ════════════════════════════════════════════════════════════════
 * 2. Lectura de una HTTP request completa
 *
 * Leemos en un buffer hasta encontrar el final de los headers
 * (\r\n\r\n) y luego leemos el body segun Content-Length.
 * ════════════════════════════════════════════════════════════════ */

/*
 * Busca la cabecera Content-Length en los headers y devuelve su valor.
 * Devuelve 0 si no la encuentra.
 */
static int leer_content_length(const char *headers) {
    const char *p = strstr(headers, "Content-Length:");
    if (!p) p = strstr(headers, "content-length:");
    if (!p) return 0;
    p += 15;  /* saltar "Content-Length:" */
    while (*p == ' ') p++;
    return atoi(p);
}

/*
 * Lee la request HTTP completa del socket `s` al buffer `buf`.
 * Devuelve el numero total de bytes leidos, o -1 si hay error.
 */
static int leer_request(SOCKET s, char *buf, int max) {
    int total = 0;

    /* Leer hasta encontrar el fin de headers \r\n\r\n */
    while (total < max - 1) {
        int n = recv(s, buf + total, 1, 0);
        if (n <= 0) return -1;
        total++;
        buf[total] = '\0';
        if (total >= 4 &&
            buf[total-4] == '\r' && buf[total-3] == '\n' &&
            buf[total-2] == '\r' && buf[total-1] == '\n') {
            break;
        }
    }

    /* Leer el body segun Content-Length */
    int cl = leer_content_length(buf);
    if (cl > 0) {
        if (total + cl >= max) cl = max - total - 1;
        int leido = 0;
        while (leido < cl) {
            int n = recv(s, buf + total + leido, cl - leido, 0);
            if (n <= 0) break;
            leido += n;
        }
        total += leido;
        buf[total] = '\0';
    }

    return total;
}

/* ════════════════════════════════════════════════════════════════
 * 3. Envio de respuestas HTTP
 * ════════════════════════════════════════════════════════════════ */

static void enviar_todo(SOCKET s, const char *data, int len) {
    int enviado = 0;
    while (enviado < len) {
        int n = send(s, data + enviado, len - enviado, 0);
        if (n <= 0) break;
        enviado += n;
    }
}

/* Respuesta JSON con status y cuerpo ya formateado */
static void responder_json(SOCKET s, int status, const char *body) {
    char headers[512];
    int blen = (int)strlen(body);
    snprintf(headers, sizeof(headers),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, blen);
    enviar_todo(s, headers, (int)strlen(headers));
    enviar_todo(s, body, blen);
}

/* Respuesta CORS preflight */
static void responder_cors(SOCKET s) {
    const char *r =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    enviar_todo(s, r, (int)strlen(r));
}

/* Servir un archivo estatico (para index.html) */
static void servir_archivo(SOCKET s, const char *ruta) {
    FILE *f = fopen(ruta, "rb");
    if (!f) {
        const char *err =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 13\r\n"
            "Connection: close\r\n"
            "\r\n"
            "404 Not Found";
        enviar_todo(s, err, (int)strlen(err));
        return;
    }

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);

    char headers[256];
    snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        tam);
    enviar_todo(s, headers, (int)strlen(headers));

    char chunk[4096];
    int n;
    while ((n = (int)fread(chunk, 1, sizeof(chunk), f)) > 0)
        enviar_todo(s, chunk, n);

    fclose(f);
}

/* ════════════════════════════════════════════════════════════════
 * 4. Logica de compilacion y ejecucion
 * ════════════════════════════════════════════════════════════════ */

static void asegurar_temp(void) {
    CreateDirectoryA(RUTA_TEMP, NULL);
}

/*
 * Escribe codigo al .scara temporal, ejecuta scara.exe y captura
 * todo su stdout+stderr en output_buf.
 * Devuelve el codigo de salida del proceso (0 = exito).
 */
static int compilar(const char *codigo, char *output_buf, int buf_max) {
    asegurar_temp();

    /* Escribir el codigo fuente al archivo temporal */
    FILE *f = fopen(RUTA_SCARA_IN, "w");
    if (!f) {
        snprintf(output_buf, buf_max,
                 "Error: no se pudo crear %s", RUTA_SCARA_IN);
        return -1;
    }
    fputs(codigo, f);
    fclose(f);

    /*
     * Usamos CreateProcess en lugar de _popen para evitar que cmd.exe
     * interprete las rutas con espacios. CreateProcess recibe el ejecutable
     * y los argumentos por separado, sin necesidad de escaping de comillas.
     *
     * Para capturar stdout+stderr redirigimos ambos a un PIPE anonimo:
     *   hWrite  — extremo de escritura que hereda scara.exe
     *   hRead   — extremo de lectura que nosotros leemos
     */
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;   /* el hijo puede usar hWrite */

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        snprintf(output_buf, buf_max, "Error: CreatePipe fallo");
        return -1;
    }
    /* El extremo de lectura NO debe heredarse al hijo */
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    /* Construir la linea de comandos: "ruta\scara.exe" "ruta\input.scara"
     * Las comillas dentro del string de argumentos son literales — cmd.exe
     * no interviene aqui. */
    char cmdline[MAX_PATH * 2 + 16];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" \"%s\" --no-run",
             RUTA_SCARA, RUTA_SCARA_IN);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;   /* stdout → pipe */
    si.hStdError  = hWrite;   /* stderr → mismo pipe */
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    BOOL ok = CreateProcessA(
        RUTA_SCARA,   /* lpApplicationName  — ruta del exe sin comillas */
        cmdline,      /* lpCommandLine       — argv[0] + argumentos     */
        NULL, NULL,
        TRUE,         /* bInheritHandles — el hijo hereda hWrite         */
        CREATE_NO_WINDOW,
        NULL, NULL,
        &si, &pi
    );

    CloseHandle(hWrite);   /* cerrar nuestra copia — el hijo tiene la suya */

    if (!ok) {
        CloseHandle(hRead);
        snprintf(output_buf, buf_max,
                 "Error: CreateProcess fallo (codigo %lu)", GetLastError());
        return -1;
    }

    /* Leer todo el output del proceso */
    int total = 0;
    output_buf[0] = '\0';
    char chunk[512];
    DWORD leido;
    while (ReadFile(hRead, chunk, sizeof(chunk) - 1, &leido, NULL) && leido > 0) {
        chunk[leido] = '\0';
        if (total + (int)leido < buf_max - 1) {
            memcpy(output_buf + total, chunk, leido);
            total += leido;
            output_buf[total] = '\0';
        }
    }
    CloseHandle(hRead);

    /* Esperar a que el proceso termine y obtener su codigo de salida */
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)exit_code;
}

/* ════════════════════════════════════════════════════════════════
 * 5. Despachar una conexion entrante
 * ════════════════════════════════════════════════════════════════ */

static void manejar_conexion(SOCKET cliente) {
    static char buf[131072];   /* 128 KB — suficiente para el codigo fuente */
    int n = leer_request(cliente, buf, sizeof(buf));
    if (n <= 0) { closesocket(cliente); return; }

    /* Extraer metodo y URI de la primera linea: "METODO /uri HTTP/1.1" */
    char metodo[16] = "", uri[256] = "";
    sscanf(buf, "%15s %255s", metodo, uri);

    printf("[%s] %s\n", metodo, uri);

    /* ── OPTIONS (preflight CORS) ── */
    if (strcmp(metodo, "OPTIONS") == 0) {
        responder_cors(cliente);
        closesocket(cliente);
        return;
    }

    /* ── GET / → index.html ── */
    if (strcmp(metodo, "GET") == 0 && strcmp(uri, "/") == 0) {
        servir_archivo(cliente, RUTA_INDEX);
        closesocket(cliente);
        return;
    }

    /* ── POST /compile ── */
    if (strcmp(metodo, "POST") == 0 && strcmp(uri, "/compile") == 0) {
        /* El body empieza despues de \r\n\r\n */
        char *body = strstr(buf, "\r\n\r\n");
        if (!body) {
            responder_json(cliente, 400,
                "{\"ok\":false,\"output\":\"Request mal formada\"}");
            closesocket(cliente);
            return;
        }
        body += 4;  /* saltar el \r\n\r\n */

        char codigo[65536];
        if (!json_leer_code(body, codigo, sizeof(codigo))) {
            responder_json(cliente, 400,
                "{\"ok\":false,\"output\":\"JSON invalido: falta campo code\"}");
            closesocket(cliente);
            return;
        }

        char output[65536];
        int ret = compilar(codigo, output, sizeof(output));

        char output_esc[131072];
        json_escape(output, output_esc, sizeof(output_esc));

        char respuesta[131072 + 64];
        snprintf(respuesta, sizeof(respuesta),
                 "{\"ok\":%s,\"output\":\"%s\"}",
                 (ret == 0) ? "true" : "false",
                 output_esc);

        responder_json(cliente, 200, respuesta);
        closesocket(cliente);
        return;
    }

    /* GET /run — lanzar input.exe con CreateProcess sin bloquear.
     * Pasamos RUTA_TEMP como working directory para que el exe
     * encuentre arial.ttf y las DLLs de SDL2 en esa carpeta. */
    if (strcmp(metodo, "GET") == 0 && strcmp(uri, "/run") == 0) {
        STARTUPINFOA si_run;
        PROCESS_INFORMATION pi_run;
        ZeroMemory(&si_run, sizeof(si_run));
        ZeroMemory(&pi_run, sizeof(pi_run));
        si_run.cb = sizeof(si_run);

        char cmdline_run[MAX_PATH + 4];
        snprintf(cmdline_run, sizeof(cmdline_run), "\"%s\"", RUTA_EXE_OUT);

        BOOL ok = CreateProcessA(
            RUTA_EXE_OUT,   /* ejecutable             */
            cmdline_run,    /* argv                   */
            NULL, NULL,
            FALSE,
            0,              /* flags normales — hereda la consola si la tiene */
            NULL,
            RUTA_TEMP,      /* working directory = temp\ donde estan las DLLs */
            &si_run, &pi_run
        );

        if (ok) {
            /* Soltar los handles — no esperamos que termine */
            CloseHandle(pi_run.hProcess);
            CloseHandle(pi_run.hThread);
        }

        responder_json(cliente, 200,
                       ok ? "{\"ok\":true}" : "{\"ok\":false}");
        closesocket(cliente);
        return;
    }

    /* ── 404 ── */
    responder_json(cliente, 404, "{\"error\":\"ruta no encontrada\"}");
    closesocket(cliente);
}

/* ════════════════════════════════════════════════════════════════
 * 6. main — inicializar Winsock y accept loop
 * ════════════════════════════════════════════════════════════════ */

int main(void) {
    /* ── Calcular rutas absolutas desde la ubicacion real del .exe ──
     *
     * GetModuleFileNameA da la ruta completa del ejecutable, ej:
     *   C:\Users\luism\...\scara\servidor\server.exe
     *
     * dir_exe  = C:\Users\luism\...\scara\servidor
     * dir_base = C:\Users\luism\...\scara          (un nivel arriba)
     *
     * Desde dir_base construimos todas las rutas absolutas, evitando
     * cualquier problema de working directory o rutas relativas. */
    char dir_exe[MAX_PATH], dir_base[MAX_PATH];

    GetModuleFileNameA(NULL, dir_exe, sizeof(dir_exe));
    /* Cortar el nombre del .exe */
    char *s1 = strrchr(dir_exe, '\\');
    if (s1) *s1 = '\0';

    /* Copiar y subir un nivel mas */
    snprintf(dir_base, sizeof(dir_base), "%s", dir_exe);
    char *s2 = strrchr(dir_base, '\\');
    if (s2) *s2 = '\0';

    /* Rutas absolutas */
    construir_ruta(RUTA_SCARA,    dir_base, "programasCOMPI\\scara.exe");
    construir_ruta(RUTA_TEMP,     dir_base, "temp");
    construir_ruta(RUTA_SCARA_IN, dir_base, "temp\\input.scara");
    construir_ruta(RUTA_EXE_OUT,  dir_base, "temp\\input.exe");
    construir_ruta(RUTA_INDEX,    dir_base, "frontend\\index.html");

    /* Crear carpeta temp si no existe */
    CreateDirectoryA(RUTA_TEMP, NULL);

    /* ── Inicializar Winsock ── */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Error: WSAStartup fallo\n");
        return 1;
    }

    /* ── Crear socket TCP ── */
    SOCKET servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor == INVALID_SOCKET) {
        fprintf(stderr, "Error: no se pudo crear el socket\n");
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PUERTO);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(servidor, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Error: bind fallo en el puerto %d\n"
                        "  (ya hay algo corriendo en ese puerto?)\n", PUERTO);
        closesocket(servidor);
        WSACleanup();
        return 1;
    }

    listen(servidor, 8);

    printf("=== Servidor SCARA ===\n");
    printf("Escuchando en http://localhost:%d\n", PUERTO);
    printf("Backend  : %s\n", RUTA_SCARA);
    printf("Temp     : %s\n", RUTA_TEMP);
    printf("Frontend : %s\n\n", RUTA_INDEX);
    printf("Abre http://localhost:%d en tu navegador.\n", PUERTO);
    printf("Ctrl+C para detener.\n\n");

    /* ── Accept loop ── */
    for (;;) {
        struct sockaddr_in cliente_addr;
        int addr_len = sizeof(cliente_addr);
        SOCKET cliente = accept(servidor,
                                (struct sockaddr*)&cliente_addr,
                                &addr_len);
        if (cliente == INVALID_SOCKET) continue;
        manejar_conexion(cliente);
    }

    closesocket(servidor);
    WSACleanup();
    return 0;
}