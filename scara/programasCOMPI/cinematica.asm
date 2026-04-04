; cinematica.asm
; Fase 4 - Bloque de integracion C/ASM.
; Expone cinematica_ik_xy_asm con el mismo contrato que la version C.
; Este bloque ya implementa logica ASM real:
;   1) valida alcance XY en enteros (rango radial [50,350] mm)
;   2) delega calculo trigonometrico al nucleo C compartido
; Siguiente subpaso: migrar tambien el nucleo trigonometrico a ASM puro.

default rel

global cinematica_ik_xy_asm
global cinematica_ik_xy_modo_asm
extern cinematica_init_tabla_cos
extern cin_cos_tab_deg
extern cin_sin_tab_deg
extern cin_atan_lut_deg

section .text
cinematica_ik_xy_asm:
	; Firma: int cinematica_ik_xy_asm(int x, int y, int* q1, int* q2)
	; Invoca modo_asm con codo-abajo (modo=1)
	mov r10, r9      ; guardar &q2
	mov r9, r8       ; r9 = &q1
	mov r8d, 1       ; modo codo-abajo

	sub rsp, 40
	mov [rsp + 32], r10 ; 5to argumento: &q2
	call cinematica_ik_xy_modo_asm
	add rsp, 40
	ret

cinematica_ik_xy_modo_asm:
	; Firma: int cinematica_ik_xy_modo_asm(int x, int y, int modo, int* q1, int* q2)
	; rcx=x, rdx=y, r8=modo, r9=&q1, [rsp+40]=&q2

	; Windows x64 ABI:
	; rcx = x, rdx = y, r8 = modo, r9 = &q1, [rsp+40] = &q2
	; reservar shadow space + locales alineados
	sub rsp, 104

	; layout local (NO tocar [rsp..rsp+31], shadow space de llamadas):
	; [rsp+32] 5to arg saliente para llamadas C
	; [rsp+40] best_err
	; [rsp+44] best_d
	; [rsp+48] &q2
	; [rsp+56] &q1
	; [rsp+64] modo
	; [rsp+68] c2_scaled
	; [rsp+72] x
	; [rsp+76] y
	; [rsp+80] r2 (64-bit)

	; guardar x,y,modo para reuso luego de validacion
	mov dword [rsp + 72], ecx
	mov dword [rsp + 76], edx
	mov dword [rsp + 64], r8d

	; guardar &q1 y &q2
	mov [rsp + 56], r9
	mov r11, [rsp + 144]
	mov [rsp + 48], r11

	; r2 = x*x + y*y (64-bit)
	movsxd rax, dword [rsp + 72]
	imul rax, rax
	movsxd r10, dword [rsp + 76]
	imul r10, r10
	add rax, r10

	; dmin = 50^2 = 2500, dmax = 350^2 = 122500
	cmp rax, 2500
	jl .fuera
	cmp rax, 122500
	jg .fuera
	mov qword [rsp + 80], rax

	; inicializar tabla cos si hace falta
	call cinematica_init_tabla_cos

	; c2_scaled = ((r2 - 62500) * 10000) / 60000
	mov rax, qword [rsp + 80]
	sub rax, 62500
	imul rax, 10000
	cqo
	mov r10, 60000
	idiv r10
	mov dword [rsp + 68], eax

	; buscar mejor d en [0..180] minimizando |c2s - cos_tab[d]|
	lea r10, [rel cin_cos_tab_deg]
	xor ecx, ecx
	mov eax, dword [rsp + 68]
	sub eax, dword [r10]
	cdq
	xor eax, edx
	sub eax, edx
	mov dword [rsp + 40], eax
	mov dword [rsp + 44], 0

	mov ecx, 1
.loop_d:
	cmp ecx, 181
	jge .done_d
	mov eax, dword [rsp + 68]
	sub eax, dword [r10 + rcx*4]
	cdq
	xor eax, edx
	sub eax, edx
	cmp eax, dword [rsp + 40]
	jge .next_d
	mov dword [rsp + 40], eax
	mov dword [rsp + 44], ecx
.next_d:
	inc ecx
	jmp .loop_d

.done_d:
	; q2_deg_signed segun modo
	mov eax, dword [rsp + 44]
	cmp dword [rsp + 64], 0
	jne .modo_down
	neg eax
.modo_down:

	; escribir q2 en salida
	mov r11, [rsp + 48]
	mov dword [r11], eax

	; calcular k1/k2 en enteros (escala 10000):
	; k1 = 200*10000 + 150*cos(q2)
	; k2 = 150*sin(q2) con signo por modo
	lea r10, [rel cin_cos_tab_deg]
	mov ecx, dword [rsp + 44]        ; |q2| en grados
	mov eax, dword [r10 + rcx*4]
	imul eax, 150
	add eax, 2000000                 ; 200 * 10000
	mov dword [rsp + 84], eax        ; k1_scaled

	lea r10, [rel cin_sin_tab_deg]
	mov eax, dword [r10 + rcx*4]
	imul eax, 150
	cmp dword [rsp + 64], 0          ; modo 0 = codo arriba => q2 negativo
	jne .k2_ok
	neg eax
.k2_ok:
	mov dword [rsp + 88], eax        ; k2_scaled

	; q1 = atan2(y, x) - atan2(k2, k1)
	mov ecx, dword [rsp + 76]        ; dy=y
	mov edx, dword [rsp + 72]        ; dx=x
	call .atan2_deg
	mov dword [rsp + 92], eax        ; theta1

	mov ecx, dword [rsp + 88]        ; dy=k2
	mov edx, dword [rsp + 84]        ; dx=k1
	call .atan2_deg
	mov dword [rsp + 96], eax        ; theta2

	mov eax, dword [rsp + 92]
	sub eax, dword [rsp + 96]

.norm_hi:
	cmp eax, 180
	jle .norm_lo
	sub eax, 360
	jmp .norm_hi

.norm_lo:
	cmp eax, -180
	jg .store_q1
	add eax, 360
	jmp .norm_lo

.store_q1:
	mov r11, [rsp + 56]
	mov dword [r11], eax
	mov eax, 1
	add rsp, 104
	ret

.fuera:
	xor eax, eax
	add rsp, 104
	ret

; atan2_deg aproximado con LUT atan(r) para r in [0,1]
; entrada: ecx=dy, edx=dx
; salida: eax=angulo en grados [-180,180]
.atan2_deg:
	; preservar signos originales para cuadrante
	mov r10d, ecx   ; dy original
	mov r11d, edx   ; dx original

	cmp edx, 0
	jne .a_dx_nz
	cmp ecx, 0
	jne .a_dx0_nonzero
	xor eax, eax
	ret

.a_dx0_nonzero:
	mov eax, 90
	cmp ecx, 0
	jg .a_ret
	neg eax
	ret

.a_dx_nz:
	; abs(dx) -> r8d
	mov r8d, edx
	test r8d, r8d
	jge .a_absx_ok
	neg r8d
.a_absx_ok:
	; abs(dy) -> r9d
	mov r9d, ecx
	test r9d, r9d
	jge .a_absy_ok
	neg r9d
.a_absy_ok:

	cmp r8d, r9d
	jl .a_inv

	; idx = abs(dy) * 1000 / abs(dx)
	mov eax, r9d
	imul eax, eax, 1000
	cdq
	idiv r8d
	lea rdx, [rel cin_atan_lut_deg]
	mov eax, dword [rdx + rax*4]
	jmp .a_base_done

.a_inv:
	; idx = abs(dx) * 1000 / abs(dy)
	mov eax, r8d
	imul eax, eax, 1000
	cdq
	idiv r9d
	lea rdx, [rel cin_atan_lut_deg]
	mov eax, dword [rdx + rax*4]
	mov ecx, 90
	sub ecx, eax
	mov eax, ecx

.a_base_done:
	cmp r11d, 0
	jl .a_dx_neg
	cmp r10d, 0
	jge .a_ret
	neg eax
	ret

.a_dx_neg:
	mov ecx, 180
	sub ecx, eax
	mov eax, ecx
	cmp r10d, 0
	jge .a_ret
	neg eax

.a_ret:
	ret
