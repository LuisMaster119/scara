; ================================================================
; Generado por compilador SCARA  —  NASM x64 completo
; Ensamblar: nasm -f win64 prog.asm -o prog.obj
; Enlazar  : gcc prog.obj -o prog.exe -L"C:/msys64/mingw64/lib"
;            -lSDL2 -lSDL2_ttf -lmingw32 -lm -mwindows
; ================================================================

default rel

extern SDL_SetMainReady
extern SDL_Init
extern SDL_Quit
extern SDL_GetError
extern SDL_CreateWindow
extern SDL_DestroyWindow
extern SDL_CreateRenderer
extern SDL_DestroyRenderer
extern SDL_RenderClear
extern SDL_RenderPresent
extern SDL_RenderDrawLine
extern SDL_RenderDrawPoint
extern SDL_RenderFillRect
extern SDL_SetRenderDrawColor
extern SDL_SetRenderDrawBlendMode
extern SDL_RenderCopy
extern SDL_QueryTexture
extern SDL_CreateTextureFromSurface
extern SDL_DestroyTexture
extern SDL_FreeSurface
extern SDL_PollEvent
extern SDL_Delay
extern TTF_Init
extern TTF_Quit
extern TTF_GetError
extern TTF_OpenFont
extern TTF_CloseFont
extern TTF_RenderText_Blended
extern TTF_SetFontStyle
extern sprintf
extern sqrt
extern atan2
extern acos
extern cos
extern sin

section .data

    s_win_title  db "SCARA — Compilador de Lenguaje Robotico", 0
    s_font_path  db "arial.ttf", 0
    f_deg2rad    dq 0.017453292519943295
    f_rad2deg    dq 57.29577951308232
    f_one        dq 1.0
    f_neg_one    dq -1.0
    f_L1         dq 200.0
    f_L2         dq 150.0
    f_den        dq 60000.0
    f_L1sq_L2sq  dq 62500.0
    ; ── strings HUD (panel izquierdo) ────────────────────────
    s_scara_vm   db "SCARA VM", 0
    s_simulacion db "Simulacion de trayectoria", 0
    s_s_pos      db "POSICION", 0
    s_s_cin      db "CINEMATICA", 0
    s_s_vel      db "VELOCIDAD", 0
    s_s_pza      db "PINZA", 0
    s_s_prg      db "PROGRESO", 0
    s_s_vis      db "VISTA", 0
    s_btn_iso    db "ISO", 0
    s_btn_top    db "TOP", 0
    s_btn_lado   db "LADO", 0
    s_v_iso      db "Isometrica", 0
    s_v_top      db "Superior", 0
    s_v_lado     db "Lateral", 0
    s_pinza_a    db "ABIERTA", 0
    s_pinza_c    db "CERRADA", 0
    s_fmt_x      db "X : %d mm", 0
    s_fmt_y      db "Y : %d mm", 0
    s_fmt_z      db "Z : %d mm", 0
    s_fmt_q1     db "q1: %d deg", 0
    s_fmt_q2     db "q2: %d deg", 0
    s_fmt_vel    db "%d %%", 0
    s_fmt_paso   db "Paso %d / %d", 0
    s_vista_iso  db "VISTA: ISOMETRICA", 0
    s_vista_top  db "VISTA: SUPERIOR", 0
    s_vista_lad  db "VISTA: LATERAL", 0
    s_hint1      db "V: vista  SPACE: pausa", 0
    s_hint2      db "< > KF   ESC: salir", 0
    s_fmt_kf     db "KF %d/%d: %s", 0
    s_kfn0       db "", 0
    s_kfn1       db "HOME", 0
    s_kfn2       db "MOVE", 0
    s_kfn3       db "MOVEJ", 0
    s_kfn4       db "APPROACH", 0
    s_kfn5       db "DEPART", 0
    s_kfn6       db "OPEN", 0
    s_kfn7       db "CLOSE", 0
    s_kfn8       db "SPEED", 0
    s_kfn9       db "WAIT", 0
    s_kfn_unk    db "-", 0
    s_kf_names:
    dq s_kfn0, s_kfn1, s_kfn2, s_kfn3, s_kfn4
    dq s_kfn5, s_kfn6, s_kfn7, s_kfn8, s_kfn9

    global traza_len
    traza_len dd 86

    global traza_x
    traza_x        dd  0, 0, 37, 75, 112, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, \
                    187, 225, 262, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 263, 225, \
                    188, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 187, 225, 262, 300, \
                    300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 263, 225, 188, 150, 150, 150, \
                    150, 150, 150, 150, 150, 150, 150, 150, 187, 225, 262, 300, 300, 300, 300, 300, \
                    300, 300, 300, 300, 300, 300
    global traza_y
    traza_y        dd  0, 0, 50, 100, 150, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, \
                    188, 175, 163, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 162, 175, \
                    187, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 188, 175, 163, 150, \
                    150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 162, 175, 187, 200, 200, 200, \
                    200, 200, 200, 200, 200, 200, 200, 200, 188, 175, 163, 150, 150, 150, 150, 150, \
                    150, 150, 150, 150, 150, 150
    global traza_z
    traza_z        dd  0, 0, 12, 25, 37, 50, 50, 50, 50, 50, 38, 25, 13, 0, 0, 50, \
                    50, 50, 50, 50, 50, 50, 50, 50, 38, 25, 13, 0, 0, 50, 50, 50, \
                    50, 50, 50, 50, 50, 50, 38, 25, 13, 0, 0, 50, 50, 50, 50, 50, \
                    50, 50, 50, 50, 38, 25, 13, 0, 0, 50, 50, 50, 50, 50, 50, 50, \
                    50, 50, 38, 25, 13, 0, 0, 50, 50, 50, 50, 50, 50, 50, 50, 50, \
                    38, 25, 13, 0, 0, 50
    global traza_pinza
    traza_pinza    dd  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, \
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, \
                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, \
                    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, \
                    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                    0, 0, 0, 0, 1, 1
    global traza_vel
    traza_vel      dd  100, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, \
                    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, \
                    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, \
                    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, \
                    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, \
                    70, 70, 70, 70, 70, 70

    traza_kf_len dd 26
    traza_kf       dd  0, 1, 2, 6, 14, 15, 16, 20, 28, 29, 30, 34, 42, 43, 44, 48, \
                    56, 57, 58, 62, 70, 71, 72, 76, 84, 85
    traza_kf_tipo  dd  1, 8, 2, 4, 7, 5, 2, 4, 6, 5, 2, 4, 7, 5, 2, 4, \
                    6, 5, 2, 4, 7, 5, 2, 4, 6, 5

section .bss

    p_win      resq 1        ; SDL_Window*
    p_ren      resq 1        ; SDL_Renderer*
    p_font     resq 1        ; TTF_Font*  normal
    p_font_b   resq 1        ; TTF_Font*  bold
    ev_buf     resb 56       ; SDL_Event (56 bytes en x64)
    modo_vista resd 1        ; 0=ISO  1=TOP  2=LATERAL
    cur_idx    resd 1        ; indice actual en la traza
    paused     resd 1        ; 1 = animacion pausada
    kf_cur     resd 1        ; indice en kf_idx[] para navegacion

section .text

    global main

traza_get_len:
    lea  rax, [rel traza_len]
    mov  eax, [rax]
    ret

traza_get_x:
    lea  rax, [rel traza_len]
    mov  eax, [rax]
    cmp  ecx, eax
    jge  .zx
    lea  rax, [rel traza_x]
    movsxd rcx, ecx
    mov  eax, [rax + rcx*4]
    ret
.zx: xor eax, eax
    ret

traza_get_y:
    lea  rax, [rel traza_len]
    mov  eax, [rax]
    cmp  ecx, eax
    jge  .zy
    lea  rax, [rel traza_y]
    movsxd rcx, ecx
    mov  eax, [rax + rcx*4]
    ret
.zy: xor eax, eax
    ret

traza_get_z:
    lea  rax, [rel traza_len]
    mov  eax, [rax]
    cmp  ecx, eax
    jge  .zz
    lea  rax, [rel traza_z]
    movsxd rcx, ecx
    mov  eax, [rax + rcx*4]
    ret
.zz: xor eax, eax
    ret

traza_get_pinza:
    lea  rax, [rel traza_len]
    mov  eax, [rax]
    cmp  ecx, eax
    jge  .zp
    lea  rax, [rel traza_pinza]
    movsxd rcx, ecx
    mov  eax, [rax + rcx*4]
    ret
.zp: xor eax, eax
    ret

traza_get_vel:
    lea  rax, [rel traza_len]
    mov  eax, [rax]
    cmp  ecx, eax
    jge  .zv
    lea  rax, [rel traza_vel]
    movsxd rcx, ecx
    mov  eax, [rax + rcx*4]
    ret
.zv: xor eax, eax
    ret

; ================================================================
; PROYECCIONES  (x,y,z del mundo → píxeles en pantalla)
; Convencion Windows x64 leaf: rcx rdx r8 r9 [rsp+40]
; ================================================================

; ── proj_iso ── codo-arriba, vista isométrica 2D ─────────────────
; proj_iso(wx, wy, wz, *sx, *sy)
;   *sx = 350 + (wx - wy)*70/100
;   *sy = 300 - ((wx+wy)*32/100  -  wz*85/100)
proj_iso:
    mov   r11d, edx
    mov   eax,  ecx
    sub   eax,  edx
    imul  eax,  70
    cdq
    mov   r10d, 100
    idiv  r10d
    add   eax,  350
    mov   [r9], eax
    mov   r10d, ecx
    add   r10d, r11d
    imul  r10d, 32
    mov   eax,  r10d
    cdq
    mov   r10d, 100
    idiv  r10d
    mov   r10d, eax
    mov   eax,  r8d
    imul  eax,  85
    cdq
    mov   r11d, 100
    idiv  r11d
    sub   r10d, eax
    mov   eax,  300
    sub   eax,  r10d
    mov   r11,  [rsp+40]
    mov   [r11], eax
    ret

; ── proj_top ── vista superior (planta) ──────────────────────────
; proj_top(wx, wy, wz, *sx, *sy)
;   *sx = 350 + wx*75/100
;   *sy = 300 - wy*75/100
proj_top:
    mov   r11d, edx
    mov   eax,  ecx
    imul  eax,  75
    cdq
    mov   r10d, 100
    idiv  r10d
    add   eax,  350
    mov   [r9], eax
    mov   eax,  r11d
    imul  eax,  75
    cdq
    mov   r10d, 100
    idiv  r10d
    mov   r10d, 300
    sub   r10d, eax
    mov   r11,  [rsp+40]
    mov   [r11], r10d
    ret

; ── proj_lado ── vista lateral (usa SSE2 sqrtsd, sigue siendo leaf)
; proj_lado(wx, wy, wz, *sx, *sy)
;   r   = (int)sqrt(wx^2 + wy^2)
;   *sx = 40  + r*620/370
;   *sy = 560 - wz*520/370
proj_lado:
    movsxd rax, ecx
    imul   rax, rax
    movsxd r10, edx
    imul   r10, r10
    add    rax, r10
    cvtsi2sd  xmm0, rax
    sqrtsd    xmm0, xmm0
    cvttsd2si eax,  xmm0
    imul  eax,  620
    cdq
    mov   r11d, 370
    idiv  r11d
    add   eax,  40
    mov   [r9], eax
    mov   eax,  r8d
    imul  eax,  520
    cdq
    mov   r10d, 370
    idiv  r10d
    mov   r10d, 560
    sub   r10d, eax
    mov   r11,  [rsp+40]
    mov   [r11], r10d
    ret

; ── proj ── despachador según modo_vista ─────────────────────────
; proj(wx, wy, wz, *sx, *sy)  —  no-leaf: llama a sub-función
;   modo_vista: 0=ISO  1=TOP  2=LATERAL
; Stack frame: push rbp / push rbx / sub rsp,40
;   rbp+48 = arg5 (*sy)   rsp+32 = 5to arg para sub-call
proj:
    push  rbp
    mov   rbp, rsp
    push  rbx
    sub   rsp, 40
    mov   rbx, r9
    mov   r11, [rbp+48]
    mov   [rsp+32], r11
    lea   rax, [rel modo_vista]
    mov   eax, [rax]
    test  eax, eax
    jz    .piso
    cmp   eax, 1
    je    .ptop
    call  proj_lado
    jmp   .pdone
.piso:
    call  proj_iso
    jmp   .pdone
.ptop:
    call  proj_top
.pdone:
    mov   eax, [rbx]
    add   eax, 200
    mov   [rbx], eax
    lea   rsp, [rbp-8]
    pop   rbx
    pop   rbp
    ret

; ================================================================
; draw_sep(y)  — dibuja línea separadora en x=[8..192] a altura y
; rcx = y   (Win x64 leaf-like but calls SDL, so uses frame)
; Frame: push rbp/rbx  sub rsp,40  (64 bytes total)
; ================================================================
draw_sep:
    push  rbp
    mov   rbp, rsp
    push  rbx
    sub   rsp, 40
    mov   ebx, ecx
    mov   rcx, [rel p_ren]
    mov   edx, 50
    mov   r8d, 70
    mov   r9d, 110
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   rcx, [rel p_ren]
    mov   edx, 8
    mov   r8d, ebx
    mov   r9d, 192
    mov   [rsp+32], ebx
    call  SDL_RenderDrawLine
    lea   rsp, [rbp-8]
    pop   rbx
    pop   rbp
    ret

; ================================================================
; draw_circle(cx, cy, r)  —  circunferencia entera Bresenham
; Caller must call SDL_SetRenderDrawColor before this function.
; rcx=cx  rdx=cy  r8=r
; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,40  (96 bytes total)
;   rbx=cx  r12=cy  r13=d  r14=x  r15=y
; ================================================================
draw_circle:
    push  rbp
    mov   rbp, rsp
    push  rbx
    push  r12
    push  r13
    push  r14
    push  r15
    sub   rsp, 40

    mov   ebx,  ecx
    mov   r12d, edx
    mov   r14d, r8d
    xor   r15d, r15d
    mov   r13d, 1
    sub   r13d, r8d
.dc_loop:
    cmp   r14d, r15d
    jl    .dc_done

    ; ─ punto (cx+x, cy+y) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    add   edx, r14d
    mov   r8d, r12d
    add   r8d, r15d
    call  SDL_RenderDrawPoint

    ; ─ punto (cx-x, cy+y) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    sub   edx, r14d
    mov   r8d, r12d
    add   r8d, r15d
    call  SDL_RenderDrawPoint

    ; ─ punto (cx+x, cy-y) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    add   edx, r14d
    mov   r8d, r12d
    sub   r8d, r15d
    call  SDL_RenderDrawPoint

    ; ─ punto (cx-x, cy-y) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    sub   edx, r14d
    mov   r8d, r12d
    sub   r8d, r15d
    call  SDL_RenderDrawPoint

    ; ─ punto (cx+y, cy+x) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    add   edx, r15d
    mov   r8d, r12d
    add   r8d, r14d
    call  SDL_RenderDrawPoint

    ; ─ punto (cx-y, cy+x) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    sub   edx, r15d
    mov   r8d, r12d
    add   r8d, r14d
    call  SDL_RenderDrawPoint

    ; ─ punto (cx+y, cy-x) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    add   edx, r15d
    mov   r8d, r12d
    sub   r8d, r14d
    call  SDL_RenderDrawPoint

    ; ─ punto (cx-y, cy-x) ─
    mov   rcx, [rel p_ren]
    mov   edx, ebx
    sub   edx, r15d
    mov   r8d, r12d
    sub   r8d, r14d
    call  SDL_RenderDrawPoint

    ; ── actualizar Bresenham ──────────────────────────────────
    inc   r15d
    test  r13d, r13d
    js    .dc_neg
    ; d >= 0: x--,  d += 2*(y-x) + 1
    dec   r14d
    mov   eax, r15d
    sub   eax, r14d
    add   eax, eax
    inc   eax
    add   r13d, eax
    jmp   .dc_loop

.dc_neg:
    ; d < 0:  d += 2*y + 1
    mov   eax, r15d
    add   eax, eax
    inc   eax
    add   r13d, eax
    jmp   .dc_loop

.dc_done:
    add   rsp, 40
    pop   r15
    pop   r14
    pop   r13
    pop   r12
    pop   rbx
    pop   rbp
    ret

; ================================================================
; draw_arc(cx, cy, r, a0_deg, a1_deg)
;   Dibuja arco de a0_deg a a1_deg (inclusive) usando cos/sin CRT
;   rcx=cx  rdx=cy  r8=r  r9=a0_deg  [rsp+40]=a1_deg  (en caller)
; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,40  (96 bytes)
;   rbx=cx  r12=cy  r13=r  r14=angulo_actual  r15=a1_deg
;   [rsp+32] = px temporal entre llamada cos y llamada sin
; ================================================================
draw_arc:
    push  rbp
    mov   rbp, rsp
    push  rbx
    push  r12
    push  r13
    push  r14
    push  r15
    sub   rsp, 40

    mov   ebx,  ecx
    mov   r12d, edx
    mov   r13d, r8d
    mov   r14d, r9d
    mov   r15d, [rbp+48]
.da_loop:
    cmp   r14d, r15d
    jg    .da_done

    ; ── calcular cos(angulo) ──────────────────────────────────
    cvtsi2sd  xmm0, r14d
    mulsd     xmm0, [rel f_deg2rad]
    call      cos
    ; px = (int)(cx + r * cos)
    cvtsi2sd  xmm1, r13d
    mulsd     xmm0, xmm1
    cvtsi2sd  xmm1, ebx
    addsd     xmm0, xmm1
    cvttsd2si eax,  xmm0
    mov       [rsp+32], eax
    ; ── calcular sin(angulo) ──────────────────────────────────
    cvtsi2sd  xmm0, r14d
    mulsd     xmm0, [rel f_deg2rad]
    call      sin
    ; py = (int)(cy + r * sin)
    cvtsi2sd  xmm1, r13d
    mulsd     xmm0, xmm1
    cvtsi2sd  xmm1, r12d
    addsd     xmm0, xmm1
    cvttsd2si r8d,  xmm0
    ; ── SDL_RenderDrawPoint(ren, px, py) ─────────────────────
    mov       rcx, [rel p_ren]
    mov       edx, [rsp+32]
    call      SDL_RenderDrawPoint

    inc   r14d
    jmp   .da_loop

.da_done:
    add   rsp, 40
    pop   r15
    pop   r14
    pop   r13
    pop   r12
    pop   rbx
    pop   rbp
    ret

; ================================================================
; ik_codo(x, y, modo, *q1_deg, *q2_deg) -> eax=1 ok / 0 fuera
;   rcx=x  rdx=y  r8=modo  r9=*q1_deg  [rsp+40]=*q2_deg
; L1=200  L2=150   CODO_ARRIBA=0  CODO_ABAJO=1
; Algoritmo:
;   c2 = (x^2+y^2 - 62500) / 60000     (cos de q2)
;   q2 = acos(c2)  [negado si CODO_ARRIBA]
;   k1 = L1 + L2*cos(q2)  ,  k2 = L2*sin(q2)
;   q1 = atan2(y,x) - atan2(k2,k1)     (en radianes)
; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,72  (128 bytes)
;   rbx=x  r12=y  r13=modo  r14=*q1_deg  r15=*q2_deg
;   stack locals (fuera del shadow [0..31]):
;     [rsp+32]=q2  [rsp+40]=k1  [rsp+48]=k2  [rsp+56]=atan2(y,x)
; ================================================================
ik_codo:
    push  rbp
    mov   rbp, rsp
    push  rbx
    push  r12
    push  r13
    push  r14
    push  r15
    sub   rsp, 72

    mov   ebx,  ecx
    mov   r12d, edx
    mov   r13d, r8d
    mov   r14,  r9
    mov   r15,  [rbp+48]
    ; ── r2 = x^2 + y^2 ───────────────────────────────────────
    cvtsi2sd  xmm0, ebx
    mulsd     xmm0, xmm0
    cvtsi2sd  xmm1, r12d
    mulsd     xmm1, xmm1
    addsd     xmm0, xmm1
    ; ── c2 = (r2 - 62500) / 60000 ────────────────────────────
    subsd     xmm0, [rel f_L1sq_L2sq]
    divsd     xmm0, [rel f_den]
    ; ── rango: -1 <= c2 <= 1 ──────────────────────────────────
    ucomisd   xmm0, [rel f_neg_one]
    jb        .ik_fail
    ucomisd   xmm0, [rel f_one]
    ja        .ik_fail
    ; ── q2 = acos(c2) ────────────────────────────────────────
    call      acos
    ; ── si CODO_ARRIBA (modo=0): q2 = -q2 ───────────────────
    test      r13d, r13d
    jnz       .ik_modo_abajo
    xorpd     xmm1, xmm1
    subsd     xmm1, xmm0
    movsd     xmm0, xmm1
.ik_modo_abajo:
    movsd     [rsp+32], xmm0
    ; ── k1 = L1 + L2*cos(q2) ─────────────────────────────────
    call      cos
    mulsd     xmm0, [rel f_L2]
    addsd     xmm0, [rel f_L1]
    movsd     [rsp+40], xmm0
    ; ── k2 = L2*sin(q2) ──────────────────────────────────────
    movsd     xmm0, [rsp+32]
    call      sin
    mulsd     xmm0, [rel f_L2]
    movsd     [rsp+48], xmm0
    ; ── atan2(y, x) ───────────────────────────────────────────
    cvtsi2sd  xmm0, r12d
    cvtsi2sd  xmm1, ebx
    call      atan2
    movsd     [rsp+56], xmm0
    ; ── atan2(k2, k1) ─────────────────────────────────────────
    movsd     xmm0, [rsp+48]
    movsd     xmm1, [rsp+40]
    call      atan2
    ; ── q1_rad = atan2(y,x) - atan2(k2,k1) ──────────────────
    movsd     xmm1, xmm0
    movsd     xmm0, [rsp+56]
    subsd     xmm0, xmm1
    ; ── q1 en grados + normalizar a [-180,180] ───────────────
    mulsd     xmm0, [rel f_rad2deg]
    cvtsd2si  eax,  xmm0
.ik_n1_hi:
    cmp       eax, 180
    jle       .ik_n1_lo
    sub       eax, 360
    jmp       .ik_n1_hi
.ik_n1_lo:
    cmp       eax, -180
    jg        .ik_n1_ok
    add       eax, 360
    jmp       .ik_n1_lo
.ik_n1_ok:
    mov       [r14], eax
    ; ── q2 en grados ─────────────────────────────────────────
    movsd     xmm0, [rsp+32]
    mulsd     xmm0, [rel f_rad2deg]
    cvtsd2si  eax,  xmm0
    mov       [r15], eax
    mov       eax, 1
    jmp       .ik_ret

.ik_fail:
    xor       eax, eax
.ik_ret:
    add       rsp, 72
    pop       r15
    pop       r14
    pop       r13
    pop       r12
    pop       rbx
    pop       rbp
    ret

; ================================================================
; draw_text(font*, text*, x, y, color_rgba)
;   rcx=font  rdx=text  r8d=x  r9d=y  [rsp+40]=color (en caller)
;   color = RGBA empaquetado en uint32  ej. 0xFFFFFFFF = blanco
; Frame: push rbp/rbx/r12/r13/r14  sub rsp,80  (128 bytes)
;   rbx=font→surface  r12=text→texture  r13d=x  r14d=y
;   [rsp+32] = &h_local para QueryTexture (5to arg)
;   [rsp+48..63] = SDL_Rect dst   [rsp+64]=w   [rsp+68]=h
; ================================================================
draw_text:
    push  rbp
    mov   rbp, rsp
    push  rbx
    push  r12
    push  r13
    push  r14
    sub   rsp, 80

    mov   rbx,  rcx
    mov   r12,  rdx
    mov   r13d, r8d
    mov   r14d, r9d
    ; ── TTF_RenderText_Blended(font, text, color) ──────────────
    mov   rcx, rbx
    mov   rdx, r12
    mov   r8d, [rbp+48]
    call  TTF_RenderText_Blended
    test  rax, rax
    jz    .dt_ret
    mov   rbx, rax
    ; ── SDL_CreateTextureFromSurface(ren, surface) ─────────────
    mov   rcx, [rel p_ren]
    mov   rdx, rbx
    call  SDL_CreateTextureFromSurface
    mov   r12, rax
    ; ── SDL_FreeSurface(surface) — liberar siempre ─────────────
    mov   rcx, rbx
    call  SDL_FreeSurface
    test  r12, r12
    jz    .dt_ret
    ; ── SDL_QueryTexture(tex, NULL, NULL, &w, &h) ───────────────
    mov   rcx, r12
    xor   edx, edx
    xor   r8d, r8d
    lea   r9,  [rsp+64]
    lea   rax, [rsp+68]
    mov   [rsp+32], rax
    call  SDL_QueryTexture
    ; ── armar SDL_Rect dst = { x, y, w, h } ─────────────────────
    mov   eax, r13d
    mov   [rsp+48], eax
    mov   eax, r14d
    mov   [rsp+52], eax
    mov   eax, [rsp+64]
    mov   [rsp+56], eax
    mov   eax, [rsp+68]
    mov   [rsp+60], eax
    ; ── SDL_RenderCopy(ren, tex, NULL, &dst) ────────────────────
    mov   rcx, [rel p_ren]
    mov   rdx, r12
    xor   r8d, r8d
    lea   r9,  [rsp+48]
    call  SDL_RenderCopy
    ; ── SDL_DestroyTexture(tex) ──────────────────────────────────
    mov   rcx, r12
    call  SDL_DestroyTexture
.dt_ret:
    add   rsp, 80
    pop   r14
    pop   r13
    pop   r12
    pop   rbx
    pop   rbp
    ret

; ================================================================
; draw_hud(idx)  — panel izquierdo 200px, igual que input_vis.c
; rcx = idx
; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,168  (224 bytes)
;   rbx=idx  r12d=wx  r13d=wy  r14d=wz  r15d=vel
;   [rsp+48]=q1_deg  [rsp+52]=q2_deg  [rsp+56]=pinza
;   [rsp+60]=vel_bar_w  [rsp+64..127]=text_buf
;   [rsp+128..143]=rect1  [rsp+144..159]=rect2
; ================================================================
draw_hud:
    push  rbp
    mov   rbp, rsp
    push  rbx
    push  r12
    push  r13
    push  r14
    push  r15
    sub   rsp, 168

    mov   ebx, ecx
    ; ── cargar traza[idx] ────────────────────────────────────
    lea   r10, [rel traza_x]
    mov   r12d, [r10 + rbx*4]
    lea   r10, [rel traza_y]
    mov   r13d, [r10 + rbx*4]
    lea   r10, [rel traza_z]
    mov   r14d, [r10 + rbx*4]
    lea   r10, [rel traza_pinza]
    mov   eax,  [r10 + rbx*4]
    mov   [rsp+56], eax
    lea   r10, [rel traza_vel]
    mov   r15d, [r10 + rbx*4]

    ; ── IK ───────────────────────────────────────────────────
    mov   ecx, r12d
    mov   edx, r13d
    xor   r8d, r8d
    lea   r9,  [rsp+48]
    lea   rax, [rsp+52]
    mov   [rsp+32], rax
    call  ik_codo

    ; vel_bar_w = 176 * vel / 100
    mov   eax, r15d
    imul  eax, 176
    cdq
    mov   ecx, 100
    idiv  ecx
    mov   [rsp+60], eax

    ; ── fondo panel con blend ────────────────────────────────
    mov   rcx, [rel p_ren]
    mov   edx, 1
    call  SDL_SetRenderDrawBlendMode
    mov   rcx, [rel p_ren]
    mov   edx, 10
    mov   r8d, 10
    mov   r9d, 20
    mov   dword [rsp+32], 210
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+128], 0
    mov   dword [rsp+132], 0
    mov   dword [rsp+136], 200
    mov   dword [rsp+140], 600
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+128]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_ren]
    xor   edx, edx
    call  SDL_SetRenderDrawBlendMode

    ; ── borde derecho (60,80,120) ────────────────────────────
    mov   rcx, [rel p_ren]
    mov   edx, 60
    mov   r8d, 80
    mov   r9d, 120
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   rcx, [rel p_ren]
    mov   edx, 200
    xor   r8d, r8d
    mov   r9d, 200
    mov   dword [rsp+32], 600
    call  SDL_RenderDrawLine

    ; ── 'SCARA VM' bold azul ─────────────────────────────────
    mov   rcx, [rel p_font_b]
    lea   rdx, [rel s_scara_vm]
    mov   r8d, 12
    mov   r9d, 12
    mov   dword [rsp+32], 0xFFFFB464
    call  draw_text

    ; ── subtítulo gris ───────────────────────────────────────
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_simulacion]
    mov   r8d, 12
    mov   r9d, 32
    mov   dword [rsp+32], 0xFFA08C8C
    call  draw_text

    mov   ecx, 56
    call  draw_sep

    ; ── POSICION ─────────────────────────────────────────────
    mov   rcx, [rel p_font_b]
    lea   rdx, [rel s_s_pos]
    mov   r8d, 12
    mov   r9d, 64
    mov   dword [rsp+32], 0xFFC8B4B4
    call  draw_text
    lea   rcx, [rsp+64]
    lea   rdx, [rel s_fmt_x]
    mov   r8d, r12d
    call  sprintf
    mov   rcx, [rel p_font]
    lea   rdx, [rsp+64]
    mov   r8d, 12
    mov   r9d, 84
    mov   dword [rsp+32], 0xFFFFDCDC
    call  draw_text

    lea   rcx, [rsp+64]
    lea   rdx, [rel s_fmt_y]
    mov   r8d, r13d
    call  sprintf
    mov   rcx, [rel p_font]
    lea   rdx, [rsp+64]
    mov   r8d, 12
    mov   r9d, 104
    mov   dword [rsp+32], 0xFFFFDCDC
    call  draw_text

    lea   rcx, [rsp+64]
    lea   rdx, [rel s_fmt_z]
    mov   r8d, r14d
    call  sprintf
    mov   rcx, [rel p_font]
    lea   rdx, [rsp+64]
    mov   r8d, 12
    mov   r9d, 124
    mov   dword [rsp+32], 0xFFFFDCDC
    call  draw_text

    mov   ecx, 148
    call  draw_sep

    ; ── CINEMATICA ───────────────────────────────────────────
    mov   rcx, [rel p_font_b]
    lea   rdx, [rel s_s_cin]
    mov   r8d, 12
    mov   r9d, 156
    mov   dword [rsp+32], 0xFFC8B4B4
    call  draw_text

    lea   rcx, [rsp+64]
    lea   rdx, [rel s_fmt_q1]
    mov   r8d, [rsp+48]
    call  sprintf
    mov   rcx, [rel p_font]
    lea   rdx, [rsp+64]
    mov   r8d, 12
    mov   r9d, 176
    mov   dword [rsp+32], 0xFFB4DCB4
    call  draw_text

    lea   rcx, [rsp+64]
    lea   rdx, [rel s_fmt_q2]
    mov   r8d, [rsp+52]
    call  sprintf
    mov   rcx, [rel p_font]
    lea   rdx, [rsp+64]
    mov   r8d, 12
    mov   r9d, 196
    mov   dword [rsp+32], 0xFFB4DCB4
    call  draw_text

    mov   ecx, 220
    call  draw_sep

    ; ── VELOCIDAD ────────────────────────────────────────────
    mov   rcx, [rel p_font_b]
    lea   rdx, [rel s_s_vel]
    mov   r8d, 12
    mov   r9d, 228
    mov   dword [rsp+32], 0xFFC8B4B4
    call  draw_text

    lea   rcx, [rsp+64]
    lea   rdx, [rel s_fmt_vel]
    mov   r8d, r15d
    call  sprintf
    mov   rcx, [rel p_font]
    lea   rdx, [rsp+64]
    mov   r8d, 12
    mov   r9d, 248
    mov   dword [rsp+32], 0xFF32B4FF
    call  draw_text

    ; ── barra velocidad ──────────────────────────────────────
    mov   rcx, [rel p_ren]
    mov   edx, 40
    mov   r8d, 40
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+128], 12
    mov   dword [rsp+132], 268
    mov   dword [rsp+136], 176
    mov   dword [rsp+140], 10
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+128]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_ren]
    mov   edx, 255
    mov   r8d, 160
    mov   r9d, 40
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+128], 12
    mov   dword [rsp+132], 268
    mov   eax, [rsp+60]
    mov   [rsp+136], eax
    mov   dword [rsp+140], 10
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+128]
    call  SDL_RenderFillRect

    mov   ecx, 288
    call  draw_sep

    ; ── PINZA ────────────────────────────────────────────────
    mov   rcx, [rel p_font_b]
    lea   rdx, [rel s_s_pza]
    mov   r8d, 12
    mov   r9d, 296
    mov   dword [rsp+32], 0xFFC8B4B4
    call  draw_text

    cmp   dword [rsp+56], 0
    jne   .dh_cerrada
    mov   rcx, [rel p_ren]
    mov   edx, 30
    mov   r8d, 200
    mov   r9d, 80
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 12
    mov   dword [rsp+148], 316
    mov   dword [rsp+152], 14
    mov   dword [rsp+156], 14
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_pinza_a]
    mov   r8d, 34
    mov   r9d, 316
    mov   dword [rsp+32], 0xFF50DC1E
    call  draw_text
    jmp   .dh_pinza_done
.dh_cerrada:
    mov   rcx, [rel p_ren]
    mov   edx, 220
    mov   r8d, 50
    mov   r9d, 50
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 12
    mov   dword [rsp+148], 316
    mov   dword [rsp+152], 14
    mov   dword [rsp+156], 14
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_pinza_c]
    mov   r8d, 34
    mov   r9d, 316
    mov   dword [rsp+32], 0xFF3232DC
    call  draw_text
.dh_pinza_done:

    mov   ecx, 342
    call  draw_sep

    ; ── PROGRESO ─────────────────────────────────────────────
    mov   rcx, [rel p_font_b]
    lea   rdx, [rel s_s_prg]
    mov   r8d, 12
    mov   r9d, 350
    mov   dword [rsp+32], 0xFFC8B4B4
    call  draw_text

    lea   rcx, [rsp+64]
    lea   rdx, [rel s_fmt_paso]
    mov   r8d, ebx
    lea   r10, [rel traza_len]
    mov   r9d, [r10]
    call  sprintf
    mov   rcx, [rel p_font]
    lea   rdx, [rsp+64]
    mov   r8d, 12
    mov   r9d, 368
    mov   dword [rsp+32], 0xFFA08C8C
    call  draw_text

    ; ── barra progreso ───────────────────────────────────────
    lea   r10, [rel traza_len]
    mov   ecx, [r10]
    cmp   ecx, 1
    jle   .dh_pb_full
    dec   ecx
    mov   eax, 176
    imul  eax, ebx
    cdq
    idiv  ecx
    jmp   .dh_pb_w_ok
.dh_pb_full:
    mov   eax, 176
.dh_pb_w_ok:
    mov   [rsp+52], eax
    mov   rcx, [rel p_ren]
    mov   edx, 40
    mov   r8d, 40
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+128], 12
    mov   dword [rsp+132], 384
    mov   dword [rsp+136], 176
    mov   dword [rsp+140], 8
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+128]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_ren]
    mov   edx, 70
    mov   r8d, 130
    mov   r9d, 220
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+128], 12
    mov   dword [rsp+132], 384
    mov   eax, [rsp+52]
    mov   [rsp+136], eax
    mov   dword [rsp+140], 8
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+128]
    call  SDL_RenderFillRect

    ; ── KF tick marks en barra HUD ───────────────────────────
    lea   r13, [rel traza_kf_len]
    mov   r13d, [r13]
    test  r13d, r13d
    jz    .dh_kf_done
    lea   r15, [rel traza_len]
    mov   r15d, [r15]
    cmp   r15d, 1
    jle   .dh_kf_done
    dec   r15d
    mov   rcx, [rel p_ren]
    mov   edx, 255
    mov   r8d, 220
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    lea   r14, [rel traza_kf]
    xor   r12d, r12d
.dh_kf_tick_loop:
    cmp   r12d, r13d
    jge   .dh_kf_done
    movsxd rax, dword [r14 + r12*4]
    imul  eax, 176
    cdq
    idiv  r15d
    add   eax, 12
    mov   rcx, [rel p_ren]
    mov   edx, eax
    mov   r8d, 381
    mov   r9d, eax
    mov   dword [rsp+32], 394
    call  SDL_RenderDrawLine
    inc   r12d
    jmp   .dh_kf_tick_loop
.dh_kf_done:

    ; ── nombre instruccion actual (busqueda dinamica) ────────
    lea   rax, [rel traza_kf_len]
    mov   r13d, [rax]
    test  r13d, r13d
    jz    .dh_kf_text_done
    lea   r14, [rel traza_kf]
    xor   r12d, r12d
    xor   r15d, r15d
.dh_find_kf_loop:
    cmp   r15d, r13d
    jge   .dh_find_kf_done
    movsxd rax, dword [r14 + r15*4]
    cmp   rax, rbx
    jg    .dh_find_kf_done
    mov   r12d, r15d
    inc   r15d
    jmp   .dh_find_kf_loop
.dh_find_kf_done:
    lea   rax, [rel traza_kf_tipo]
    movsxd r14, dword [rax + r12*4]
    cmp   r14d, 0
    jl    .dh_kf_unk_name
    cmp   r14d, 9
    jg    .dh_kf_unk_name
    lea   rax, [rel s_kf_names]
    mov   r15, [rax + r14*8]
    jmp   .dh_kf_name_ok
.dh_kf_unk_name:
    lea   r15, [rel s_kfn_unk]
.dh_kf_name_ok:
    mov   rcx, [rel p_font]
    mov   rdx, r15
    mov   r8d, 12
    mov   r9d, 420
    mov   dword [rsp+32], 0xFF3CDCFF
    call  draw_text

.dh_kf_text_done:

    mov   ecx, 438
    call  draw_sep

    ; ── VISTA ────────────────────────────────────────────────
    mov   rcx, [rel p_font_b]
    lea   rdx, [rel s_s_vis]
    mov   r8d, 12
    mov   r9d, 446
    mov   dword [rsp+32], 0xFFC8B4B4
    call  draw_text

    ; botón ISO (x=12, índice=0) ─────────────────────────────
    lea   rax, [rel modo_vista]
    mov   eax, [rax]
    test  eax, eax
    je    .dh_v0_act
    mov   rcx, [rel p_ren]
    mov   edx, 40
    mov   r8d, 40
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 12
    mov   dword [rsp+148], 464
    mov   dword [rsp+152], 50
    mov   dword [rsp+156], 18
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_btn_iso]
    mov   r8d, 18
    mov   r9d, 466
    mov   dword [rsp+32], 0xFF826464
    call  draw_text
    jmp   .dh_v0_done
.dh_v0_act:
    mov   rcx, [rel p_ren]
    mov   edx, 70
    mov   r8d, 130
    mov   r9d, 220
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 12
    mov   dword [rsp+148], 464
    mov   dword [rsp+152], 50
    mov   dword [rsp+156], 18
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_btn_iso]
    mov   r8d, 18
    mov   r9d, 466
    mov   dword [rsp+32], 0xFFFFFFFF
    call  draw_text
.dh_v0_done:

    ; botón TOP (x=68, índice=1) ─────────────────────────────
    lea   rax, [rel modo_vista]
    mov   eax, [rax]
    cmp   eax, 1
    je    .dh_v1_act
    mov   rcx, [rel p_ren]
    mov   edx, 40
    mov   r8d, 40
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 68
    mov   dword [rsp+148], 464
    mov   dword [rsp+152], 50
    mov   dword [rsp+156], 18
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_btn_top]
    mov   r8d, 74
    mov   r9d, 466
    mov   dword [rsp+32], 0xFF826464
    call  draw_text
    jmp   .dh_v1_done
.dh_v1_act:
    mov   rcx, [rel p_ren]
    mov   edx, 70
    mov   r8d, 130
    mov   r9d, 220
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 68
    mov   dword [rsp+148], 464
    mov   dword [rsp+152], 50
    mov   dword [rsp+156], 18
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_btn_top]
    mov   r8d, 74
    mov   r9d, 466
    mov   dword [rsp+32], 0xFFFFFFFF
    call  draw_text
.dh_v1_done:

    ; botón LADO (x=124, índice=2) ───────────────────────────
    lea   rax, [rel modo_vista]
    mov   eax, [rax]
    cmp   eax, 2
    je    .dh_v2_act
    mov   rcx, [rel p_ren]
    mov   edx, 40
    mov   r8d, 40
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 124
    mov   dword [rsp+148], 464
    mov   dword [rsp+152], 50
    mov   dword [rsp+156], 18
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_btn_lado]
    mov   r8d, 130
    mov   r9d, 466
    mov   dword [rsp+32], 0xFF826464
    call  draw_text
    jmp   .dh_v2_done
.dh_v2_act:
    mov   rcx, [rel p_ren]
    mov   edx, 70
    mov   r8d, 130
    mov   r9d, 220
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+144], 124
    mov   dword [rsp+148], 464
    mov   dword [rsp+152], 50
    mov   dword [rsp+156], 18
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+144]
    call  SDL_RenderFillRect
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_btn_lado]
    mov   r8d, 130
    mov   r9d, 466
    mov   dword [rsp+32], 0xFFFFFFFF
    call  draw_text
.dh_v2_done:

    ; ── nombre de vista actual ───────────────────────────────
    lea   rax, [rel modo_vista]
    mov   eax, [rax]
    cmp   eax, 1
    je    .dh_vn_top
    cmp   eax, 2
    je    .dh_vn_lado
    lea   rdx, [rel s_v_iso]
    jmp   .dh_vn_draw
.dh_vn_top:  lea   rdx, [rel s_v_top]
    jmp   .dh_vn_draw
.dh_vn_lado: lea   rdx, [rel s_v_lado]
.dh_vn_draw:
    mov   rcx, [rel p_font]
    mov   r8d, 12
    mov   r9d, 488
    mov   dword [rsp+32], 0xFFFFC88C
    call  draw_text

    ; ── hints ────────────────────────────────────────────────
    mov   rcx, [rel p_font]
    lea   rdx, [rel s_hint1]
    mov   r8d, 12
    mov   r9d, 532
    mov   dword [rsp+32], 0xFF645050
    call  draw_text

    mov   rcx, [rel p_font]
    lea   rdx, [rel s_hint2]
    mov   r8d, 12
    mov   r9d, 552
    mov   dword [rsp+32], 0xFF645050
    call  draw_text

.dh_ret:
    add   rsp, 168
    pop   r15
    pop   r14
    pop   r13
    pop   r12
    pop   rbx
    pop   rbp
    ret

; ================================================================
; draw_frame(idx) — frame completo con grid, brazo, HUD, barra
; rcx = idx
; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,120  (176 bytes)
;   rbx=idx  r12d=wx  r13d=wy  r14d=wz
;   [rsp+48]=q1_deg  [rsp+52]=q2_deg
;   [rsp+56]=base_sx [rsp+60]=base_sy
;   [rsp+64]=elbow_sx [rsp+68]=elbow_sy
;   [rsp+72]=tool_sx  [rsp+76]=tool_sy
;   [rsp+84]=ex  [rsp+88]=ey  [rsp+92]=pinza
;   [rsp+96..111]=grid_temps  (reusados para bar_rect al final)
; ================================================================
draw_frame:
    push  rbp
    mov   rbp, rsp
    push  rbx
    push  r12
    push  r13
    push  r14
    push  r15
    sub   rsp, 120

    mov   ebx, ecx

    ; ================================================================
    ; 0. Grid de fondo (30,30,50,255)
    ; ================================================================
    mov   rcx, [rel p_ren]
    mov   edx, 30
    mov   r8d, 30
    mov   r9d, 50
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor

    ; ¿vista LADO?
    lea   rax, [rel modo_vista]
    mov   eax, [rax]
    cmp   eax, 2
    je    .df_grid_lado

    ; ── grid ISO/TOP: g de -350 a 350 paso 50 ────────────────
    mov   r15d, -350
.df_grid_loop:
    cmp   r15d, 350
    jg    .df_grid_done
    ; línea en dirección X: proj(g,-350,0) → proj(g,350,0)
    mov   ecx, r15d
    mov   edx, -350
    xor   r8d, r8d
    lea   r9,  [rsp+96]
    lea   rax, [rsp+100]
    mov   [rsp+32], rax
    call  proj
    mov   ecx, r15d
    mov   edx, 350
    xor   r8d, r8d
    lea   r9,  [rsp+104]
    lea   rax, [rsp+108]
    mov   [rsp+32], rax
    call  proj
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+96]
    mov   r8d, [rsp+100]
    mov   r9d, [rsp+104]
    mov   eax, [rsp+108]
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    ; línea en dirección Y: proj(-350,g,0) → proj(350,g,0)
    mov   ecx, -350
    mov   edx, r15d
    xor   r8d, r8d
    lea   r9,  [rsp+96]
    lea   rax, [rsp+100]
    mov   [rsp+32], rax
    call  proj
    mov   ecx, 350
    mov   edx, r15d
    xor   r8d, r8d
    lea   r9,  [rsp+104]
    lea   rax, [rsp+108]
    mov   [rsp+32], rax
    call  proj
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+96]
    mov   r8d, [rsp+100]
    mov   r9d, [rsp+104]
    mov   eax, [rsp+108]
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    add   r15d, 50
    jmp   .df_grid_loop
    jmp   .df_grid_done

    ; ── grid LADO: r y z de 0 a 350 paso 50 ─────────────────
.df_grid_lado:
    mov   r15d, 0
.df_grid_lado_loop:
    cmp   r15d, 350
    jg    .df_grid_done
    ; columna a radio r=g: proj(g,0,0) → proj(g,0,350)
    mov   ecx, r15d
    xor   edx, edx
    xor   r8d, r8d
    lea   r9,  [rsp+96]
    lea   rax, [rsp+100]
    mov   [rsp+32], rax
    call  proj
    mov   ecx, r15d
    xor   edx, edx
    mov   r8d, 350
    lea   r9,  [rsp+104]
    lea   rax, [rsp+108]
    mov   [rsp+32], rax
    call  proj
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+96]
    mov   r8d, [rsp+100]
    mov   r9d, [rsp+104]
    mov   eax, [rsp+108]
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    ; fila a altura z=g: proj(0,0,g) → proj(350,0,g)
    xor   ecx, ecx
    xor   edx, edx
    mov   r8d, r15d
    lea   r9,  [rsp+96]
    lea   rax, [rsp+100]
    mov   [rsp+32], rax
    call  proj
    mov   ecx, 350
    xor   edx, edx
    mov   r8d, r15d
    lea   r9,  [rsp+104]
    lea   rax, [rsp+108]
    mov   [rsp+32], rax
    call  proj
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+96]
    mov   r8d, [rsp+100]
    mov   r9d, [rsp+104]
    mov   eax, [rsp+108]
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    add   r15d, 50
    jmp   .df_grid_lado_loop

.df_grid_done:

    ; ================================================================
    ; 1. Cargar datos de traza[idx]
    ; ================================================================
    lea   r10, [rel traza_x]
    mov   r12d, [r10 + rbx*4]
    lea   r10, [rel traza_y]
    mov   r13d, [r10 + rbx*4]
    lea   r10, [rel traza_z]
    mov   r14d, [r10 + rbx*4]
    lea   r10, [rel traza_pinza]
    mov   eax,  [r10 + rbx*4]
    mov   [rsp+92], eax

    ; ── IK ───────────────────────────────────────────────────
    mov   ecx, r12d
    mov   edx, r13d
    xor   r8d, r8d
    lea   r9,  [rsp+48]
    lea   rax, [rsp+52]
    mov   [rsp+32], rax
    call  ik_codo
    test  eax, eax
    jnz   .dframe_ik_ok
    mov   dword [rsp+48], 0
    mov   dword [rsp+52], 0
.dframe_ik_ok:

    ; ── FK: ex=L1*cos(q1)  ey=L1*sin(q1) ────────────────────
    cvtsi2sd  xmm0, dword [rsp+48]
    mulsd     xmm0, [rel f_deg2rad]
    call      cos
    mulsd     xmm0, [rel f_L1]
    cvttsd2si eax,  xmm0
    mov   [rsp+84], eax
    cvtsi2sd  xmm0, dword [rsp+48]
    mulsd     xmm0, [rel f_deg2rad]
    call      sin
    mulsd     xmm0, [rel f_L1]
    cvttsd2si eax,  xmm0
    mov   [rsp+88], eax

    ; ── proyecciones ─────────────────────────────────────────
    xor   ecx, ecx
    xor   edx, edx
    xor   r8d, r8d
    lea   r9,  [rsp+56]
    lea   rax, [rsp+60]
    mov   [rsp+32], rax
    call  proj
    mov   ecx, [rsp+84]
    mov   edx, [rsp+88]
    mov   r8d, r14d
    lea   r9,  [rsp+64]
    lea   rax, [rsp+68]
    mov   [rsp+32], rax
    call  proj
    mov   ecx, r12d
    mov   edx, r13d
    mov   r8d, r14d
    lea   r9,  [rsp+72]
    lea   rax, [rsp+76]
    mov   [rsp+32], rax
    call  proj

    ; ================================================================
    ; 2. Eslabón 1: base→codo  (70,130,220) grosor 3px
    ; ================================================================
    mov   rcx, [rel p_ren]
    mov   edx, 70
    mov   r8d, 130
    mov   r9d, 220
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    ; t=-1 (y offset)
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+56]
    mov   r8d, [rsp+60]
    dec   r8d
    mov   r9d, [rsp+64]
    mov   eax, [rsp+68]
    dec   eax
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    ; t=0
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+56]
    mov   r8d, [rsp+60]
    mov   r9d, [rsp+64]
    mov   eax, [rsp+68]
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    ; t=+1
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+56]
    mov   r8d, [rsp+60]
    inc   r8d
    mov   r9d, [rsp+64]
    mov   eax, [rsp+68]
    inc   eax
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine

    ; ================================================================
    ; 3. Eslabón 2: codo→herramienta  (180,100,40) grosor 3px
    ; ================================================================
    mov   rcx, [rel p_ren]
    mov   edx, 180
    mov   r8d, 100
    mov   r9d, 40
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+64]
    mov   r8d, [rsp+68]
    dec   r8d
    mov   r9d, [rsp+72]
    mov   eax, [rsp+76]
    dec   eax
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+64]
    mov   r8d, [rsp+68]
    mov   r9d, [rsp+72]
    mov   eax, [rsp+76]
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine
    mov   rcx, [rel p_ren]
    mov   edx, [rsp+64]
    mov   r8d, [rsp+68]
    inc   r8d
    mov   r9d, [rsp+72]
    mov   eax, [rsp+76]
    inc   eax
    mov   [rsp+32], eax
    call  SDL_RenderDrawLine

    ; ================================================================
    ; 4. Círculos: base=blanco, codo=verde, end=amarillo/rojo
    ; ================================================================
    ; base (200,200,200) r=8
    mov   rcx, [rel p_ren]
    mov   edx, 200
    mov   r8d, 200
    mov   r9d, 200
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   ecx, [rsp+56]
    mov   edx, [rsp+60]
    mov   r8d, 8
    call  draw_circle

    ; codo (100,200,100) r=6
    mov   rcx, [rel p_ren]
    mov   edx, 100
    mov   r8d, 200
    mov   r9d, 100
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   ecx, [rsp+64]
    mov   edx, [rsp+68]
    mov   r8d, 6
    call  draw_circle

    ; end-effector: amarillo=abierta(255,220,50) / rojo=cerrada(255,60,60)
    cmp   dword [rsp+92], 0
    jne   .dframe_cerrada
    mov   rcx, [rel p_ren]
    mov   edx, 255
    mov   r8d, 220
    mov   r9d, 50
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    jmp   .dframe_draw_end
.dframe_cerrada:
    mov   rcx, [rel p_ren]
    mov   edx, 255
    mov   r8d, 60
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
.dframe_draw_end:
    mov   ecx, [rsp+72]
    mov   edx, [rsp+76]
    mov   r8d, 9
    call  draw_circle

    ; ================================================================
    ; 5. HUD (panel izquierdo)
    ; ================================================================
    mov   ecx, ebx
    call  draw_hud

    ; ================================================================
    ; 6. Etiqueta de vista (top-right, x=710, y=12)
    ; ================================================================
    lea   rax, [rel modo_vista]
    mov   eax, [rax]
    cmp   eax, 1
    je    .df_lbl_top
    cmp   eax, 2
    je    .df_lbl_lado
    lea   rdx, [rel s_vista_iso]
    jmp   .df_lbl_draw
.df_lbl_top:  lea   rdx, [rel s_vista_top]
    jmp   .df_lbl_draw
.df_lbl_lado: lea   rdx, [rel s_vista_lad]
.df_lbl_draw:
    mov   rcx, [rel p_font_b]
    mov   r8d, 710
    mov   r9d, 12
    mov   dword [rsp+32], 0xC8FFB464
    call  draw_text

    ; ================================================================
    ; 7. Barra de progreso inferior (y=594, h=6)
    ; ================================================================
    lea   r10, [rel traza_len]
    mov   r15d, [r10]
    cmp   r15d, 1
    jle   .df_pb_full
    mov   eax, r15d
    dec   eax
    mov   ecx, eax
    mov   eax, 900
    imul  eax, ebx
    cdq
    idiv  ecx
    jmp   .df_pb_w_ok
.df_pb_full:
    mov   eax, 900
.df_pb_w_ok:
    mov   [rsp+92], eax
    ; fondo (30,30,50)
    mov   rcx, [rel p_ren]
    mov   edx, 30
    mov   r8d, 30
    mov   r9d, 50
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+96],  0
    mov   dword [rsp+100], 594
    mov   dword [rsp+104], 900
    mov   dword [rsp+108], 6
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+96]
    call  SDL_RenderFillRect
    ; fg (70,130,220)
    mov   rcx, [rel p_ren]
    mov   edx, 70
    mov   r8d, 130
    mov   r9d, 220
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   dword [rsp+96],  0
    mov   dword [rsp+100], 594
    mov   eax, [rsp+92]
    mov   [rsp+104], eax
    mov   dword [rsp+108], 6
    mov   rcx, [rel p_ren]
    lea   rdx, [rsp+96]
    call  SDL_RenderFillRect

    ; lineas de keyframe (amarillo 255,220,60)
    lea   r14, [rel traza_kf_len]
    mov   r14d, [r14]
    test  r14d, r14d
    jz    .df_kf_done
    mov   rcx, [rel p_ren]
    mov   edx, 255
    mov   r8d, 220
    mov   r9d, 60
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    cmp   r15d, 1
    jle   .df_kf_done
    mov   r13d, r15d
    dec   r13d
    lea   r15, [rel traza_kf]
    xor   r12d, r12d
.df_kf_loop:
    cmp   r12d, r14d
    jge   .df_kf_done
    movsxd rax, dword [r15 + r12*4]
    imul  eax, 900
    cdq
    idiv  r13d
    mov   rcx, [rel p_ren]
    mov   edx, eax
    mov   r8d, 591
    mov   r9d, eax
    mov   dword [rsp+32], 602
    call  SDL_RenderDrawLine
    inc   r12d
    jmp   .df_kf_loop
.df_kf_done:

    add   rsp, 120
    pop   r15
    pop   r14
    pop   r13
    pop   r12
    pop   rbx
    pop   rbp
    ret

; ================================================================
; main  (Paso 1 — esqueleto: abre ventana, muestra 1 frame, cierra)
; Stack frame: sub rsp,48  => 32 shadow + 16 para args 5/6
; Convencion Windows x64: rcx rdx r8 r9  [rsp+32] [rsp+40] ...
; ================================================================
main:
    push  rbp
    mov   rbp, rsp
    sub   rsp, 48

    ; SDL_SetMainReady()
    call  SDL_SetMainReady

    ; SDL_Init(SDL_INIT_VIDEO = 0x20)
    mov   ecx, 0x20
    call  SDL_Init
    test  eax, eax
    jz    .sdl_ok
    mov   eax, 1
    leave
    ret
.sdl_ok:

    ; TTF_Init()
    call  TTF_Init
    test  eax, eax
    jz    .ttf_ok
    call  SDL_Quit
    mov   eax, 1
    leave
    ret
.ttf_ok:

    ; p_font = TTF_OpenFont("arial.ttf", 16)
    lea   rcx, [rel s_font_path]
    mov   edx, 16
    call  TTF_OpenFont
    mov   [rel p_font], rax
    test  rax, rax
    jnz   .font_ok
    call  TTF_Quit
    call  SDL_Quit
    mov   eax, 1
    leave
    ret
.font_ok:

    ; p_font_b = TTF_OpenFont("arial.ttf", 16)
    lea   rcx, [rel s_font_path]
    mov   edx, 16
    call  TTF_OpenFont
    mov   [rel p_font_b], rax

    ; TTF_SetFontStyle(p_font_b, TTF_STYLE_BOLD=1)
    mov   rcx, [rel p_font_b]
    mov   edx, 1
    call  TTF_SetFontStyle

    ; SDL_CreateWindow(title, CENTERED, CENTERED, 900, 600, SHOWN)
    lea   rcx, [rel s_win_title]
    mov   edx, 0x2FFF0000          ; SDL_WINDOWPOS_CENTERED
    mov   r8d, 0x2FFF0000
    mov   r9d, 900                 ; WIN_W
    mov   dword [rsp+32], 600      ; WIN_H  (5to arg)
    mov   dword [rsp+40], 4        ; SDL_WINDOW_SHOWN (6to arg)
    call  SDL_CreateWindow
    mov   [rel p_win], rax

    ; SDL_CreateRenderer(win, -1, ACCELERATED|PRESENTVSYNC = 6)
    mov   rcx, [rel p_win]
    mov   edx, -1
    mov   r8d, 6
    call  SDL_CreateRenderer
    mov   [rel p_ren], rax

    ; SDL_SetRenderDrawColor(ren, 15, 15, 25, 255)  — fondo oscuro
    mov   rcx, [rel p_ren]
    mov   edx, 15
    mov   r8d, 15
    mov   r9d, 25
    mov   dword [rsp+32], 255      ; alpha (5to arg)
    call  SDL_SetRenderDrawColor

    ; SDL_RenderClear(ren)
    mov   rcx, [rel p_ren]
    call  SDL_RenderClear

    ; SDL_RenderPresent(ren)
    mov   rcx, [rel p_ren]
    call  SDL_RenderPresent

    ; ================================================================
    ; Paso 8b — render / event loop principal
    ; ================================================================
.ml_loop:
    ; ── vaciar cola de eventos ───────────────────────────────
.ml_poll:
    lea   rcx, [rel ev_buf]
    call  SDL_PollEvent
    test  eax, eax
    jz    .ml_no_event
    lea   rax, [rel ev_buf]
    mov   eax, [rax]
    cmp   eax, 0x100
    je    .ml_quit
    cmp   eax, 0x300
    jne   .ml_poll
    ; keysym.sym esta en SDL_KeyboardEvent a offset 20
    lea   rax, [rel ev_buf]
    mov   eax, [rax + 20]
    cmp   eax, 27
    je    .ml_quit
    cmp   eax, 32
    jne   .ml_not_space
    lea   rax, [rel paused]
    mov   ecx, [rax]
    xor   ecx, 1
    mov   [rax], ecx
    jmp   .ml_poll
.ml_not_space:
    cmp   eax, 0x4000004F
    je    .ml_kf_right
    cmp   eax, 0x40000050
    je    .ml_kf_left
    jmp   .ml_not_lr
.ml_kf_right:
    lea   r10, [rel traza_kf_len]
    mov   r11d, [r10]
    test  r11d, r11d
    jz    .ml_poll
    lea   r10, [rel cur_idx]
    mov   edx, [r10]
    lea   r10, [rel traza_kf]
    xor   ecx, ecx
    xor   eax, eax
.ml_fkf_r:
    cmp   eax, r11d
    jge   .ml_fkf_r_done
    cmp   dword [r10 + rax*4], edx
    jg    .ml_fkf_r_done
    mov   ecx, eax
    inc   eax
    jmp   .ml_fkf_r
.ml_fkf_r_done:
    inc   ecx
    cmp   ecx, r11d
    jl    .ml_right_ok
    mov   ecx, r11d
    dec   ecx
.ml_right_ok:
    lea   rax, [rel kf_cur]
    mov   [rax], ecx
    movsxd rdx, dword [r10 + rcx*4]
    lea   rax, [rel cur_idx]
    mov   [rax], edx
    lea   rax, [rel paused]
    mov   dword [rax], 1
    jmp   .ml_poll
.ml_kf_left:
    lea   r10, [rel traza_kf_len]
    mov   r11d, [r10]
    test  r11d, r11d
    jz    .ml_poll
    lea   r10, [rel cur_idx]
    mov   edx, [r10]
    lea   r10, [rel traza_kf]
    xor   ecx, ecx
    xor   eax, eax
.ml_fkf_l:
    cmp   eax, r11d
    jge   .ml_fkf_l_done
    cmp   dword [r10 + rax*4], edx
    jg    .ml_fkf_l_done
    mov   ecx, eax
    inc   eax
    jmp   .ml_fkf_l
.ml_fkf_l_done:
    test  ecx, ecx
    jz    .ml_poll
    dec   ecx
    lea   rax, [rel kf_cur]
    mov   [rax], ecx
    movsxd rdx, dword [r10 + rcx*4]
    lea   rax, [rel cur_idx]
    mov   [rax], edx
    lea   rax, [rel paused]
    mov   dword [rax], 1
    jmp   .ml_poll
.ml_not_lr:
    cmp   eax, 118
    jne   .ml_poll
    lea   rax, [rel modo_vista]
    mov   ecx, [rax]
    inc   ecx
    cmp   ecx, 3
    jl    .ml_mv_ok
    xor   ecx, ecx
.ml_mv_ok:
    mov   [rax], ecx
    jmp   .ml_poll

.ml_no_event:
    ; ── limpiar pantalla (fondo oscuro) ─────────────────────
    mov   rcx, [rel p_ren]
    mov   edx, 15
    mov   r8d, 15
    mov   r9d, 25
    mov   dword [rsp+32], 255
    call  SDL_SetRenderDrawColor
    mov   rcx, [rel p_ren]
    call  SDL_RenderClear

    ; ── dibujar frame actual ─────────────────────────────────
    lea   rax, [rel cur_idx]
    mov   ecx, [rax]
    call  draw_frame

    ; ── presentar ────────────────────────────────────────────
    mov   rcx, [rel p_ren]
    call  SDL_RenderPresent

    ; ── pausa: si paused!=0, no avanzar índice ─────────────
    lea   rax, [rel paused]
    cmp   dword [rax], 0
    jne   .ml_paused

    ; ── avanzar indice (ciclico) ──────────────────────────────
    lea   rax, [rel cur_idx]
    mov   ecx, [rax]
    inc   ecx
    lea   r10, [rel traza_len]
    cmp   ecx, [r10]
    jl    .ml_idx_ok
    xor   ecx, ecx
.ml_idx_ok:
    mov   [rax], ecx
    ; ── delay basado en velocidad del paso actual ─────────────
    lea   r10, [rel traza_vel]
    mov   ecx, [rax]
    mov   ecx, [r10 + rcx*4]
    ; delay = 220 - vel*180/100  (min 35ms)
    mov   eax, ecx
    imul  eax, 180
    cdq
    mov   ecx, 100
    idiv  ecx
    mov   ecx, 220
    sub   ecx, eax
    cmp   ecx, 35
    jge   .ml_delay_ok
    mov   ecx, 35
.ml_delay_ok:
    call  SDL_Delay
    jmp   .ml_loop

.ml_paused:
    mov   ecx, 16
    call  SDL_Delay
    jmp   .ml_loop

.ml_quit:
; ── cleanup ──────────────────────────────────────────────────────
    mov   rcx, [rel p_ren]
    call  SDL_DestroyRenderer

    mov   rcx, [rel p_win]
    call  SDL_DestroyWindow

    mov   rcx, [rel p_font]
    call  TTF_CloseFont

    mov   rcx, [rel p_font_b]
    call  TTF_CloseFont

    call  TTF_Quit
    call  SDL_Quit

    xor   eax, eax
    leave
    ret
