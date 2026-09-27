; void vm_fast(void);
;
; The interpreter's hot path in assembly. Runs bytecode from vm_pc for as
; long as the instructions are simple ones on plain values (numbers,
; variable reads and writes, comparisons, branches inside the current code
; block) and returns at the first instruction it doesn't handle, with
; vm_pc pointing at it, for the C interpreter (vm/interp.c, run()) to do.
; An instruction only changes anything once it's sure to complete here,
; so C always sees the state from before or after it.
;
; IY = vm_pc, IX = vm_sp (just past the top value). Values are 5 bytes:
; type, then 4 bytes (a float or a pointer). The offsets below are checked
; against the C structures in interp.c.

	.assume	ADL = 1

VT_UNDEF = 0
VT_REAL = 1
VT_HSTR = 3

OP_CMP_LT = 20
NOPS = 40

SC_GLOBAL = 2
SC_LOCAL = 3
V_BUILTIN_COUNT = 126

INST_VAR_IDS = 75
INST_VAR_VALS = 78
INST_NVARS = 82
FRAME_LOCALS = 8
GD_NGLOBALS = 16
STACK_BYTES = 1280

	.section	.text._vm_fast,"ax",@progbits
	.globl	_vm_fast
	.type	_vm_fast,@function
_vm_fast:
	push	ix
	ld	ix, (_vm_sp)
	ld	iy, (_vm_pc)
	; offset of the current block's first byte, for branches: blk << 10
	or	a, a
	sbc	hl, hl
	ld	a, (_vm_blk)
	ld	h, a
	add	hl, hl
	add	hl, hl
	ld	(blk_start), hl
	jp	next

; ---- leave: C does the instruction at IY ----
slow:
	ld	(_vm_pc), iy
	ld	(_vm_sp), ix
	pop	ix
	ret

; ---- dispatch ----
next:
	ld	a, (iy + 0)
	cp	a, NOPS
	jr	nc, slow
	or	a, a
	sbc	hl, hl
	ld	l, a
	push	hl
	pop	de
	add	hl, hl
	add	hl, de			; op * 3
	ld	de, optable
	add	hl, de
	ld	hl, (hl)
	jp	(hl)

; carry set if there's room for one more value
room:
	lea	hl, ix + 5
	ld	de, _vm_stack + STACK_BYTES
	or	a, a
	sbc	hl, de
	ret

; ---- constants ----
f_nop:
	inc	iy
	jp	next

f_push_real:
	call	room
	jp	nc, slow
	ld	(ix + 0), VT_REAL
	ld	hl, (iy + 1)
	ld	(ix + 1), hl
	ld	a, (iy + 4)
	ld	(ix + 4), a
	lea	ix, ix + 5
	lea	iy, iy + 5
	jp	next

f_push_undef:
	call	room
	jp	nc, slow
	or	a, a
	sbc	hl, hl
	ld	(ix + 0), VT_UNDEF
	ld	(ix + 1), hl
	ld	(ix + 4), l
	lea	ix, ix + 5
	inc	iy
	jp	next

; ---- variables ----

; The variable of the PUSH_VAR / POP_VAR at IY (op, scope, u16 id):
; HL = its value and carry set; carry clear if it isn't handled here, with
; A = 0xff when it's an instance variable the instance doesn't have.
var_addr:
	ld	de, 0
	ld	e, (iy + 2)
	ld	d, (iy + 3)		; de = id
	ld	a, (iy + 1)
	cp	a, SC_GLOBAL
	jr	z, va_global
	cp	a, SC_LOCAL
	jr	z, va_local
	or	a, a
	ret	nz			; other scopes: C (carry clear)
	; self: an instance variable, not a built-in one
	ld	hl, V_BUILTIN_COUNT - 1
	sbc	hl, de			; carry clear from "or"
	ret	nc			; id < V_BUILTIN_COUNT (carry clear)
	ld	hl, (_vm_self)
	add	hl, de
	or	a, a
	sbc	hl, de
	ret	z			; no instance (carry clear)
	jp	find_var
va_global:
	; id < gd.nglobals
	ld	hl, (_gd + GD_NGLOBALS)
	or	a, a
	sbc	hl, de
	jr	z, va_no
	jr	c, va_no
	ld	hl, (_globals)
	jr	va_index
va_local:
	ld	hl, (_vm_cur)
	add	hl, de
	or	a, a
	sbc	hl, de
	jr	z, va_no
	ld	bc, FRAME_LOCALS
	add	hl, bc
	ld	hl, (hl)
va_index:
	; hl + de * 5
	push	hl
	ex	de, hl
	push	hl
	pop	de
	add	hl, hl
	add	hl, hl
	add	hl, de
	pop	de
	add	hl, de
	scf
	ret
va_no:
	xor	a, a			; carry clear, a = 0
	ret

; Instance HL, id DE: HL = the variable's value and carry set, or carry
; clear and A = 0xff. Binary search in var_ids (sorted u16).
find_var:
	ld	bc, INST_VAR_VALS
	add	hl, bc
	ld	bc, (hl)
	push	bc			; vals
	ld	bc, INST_VAR_IDS - INST_VAR_VALS
	add	hl, bc
	ld	bc, (hl)
	push	bc			; ids
	ld	bc, INST_NVARS - INST_VAR_IDS
	add	hl, bc
	ld	bc, 0
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; bc = n
	ld	(fv_n), bc
	pop	hl
	push	hl			; p = ids
fv_loop:
	; hl = p, bc = n (upper byte 0), de = id
	ld	a, b
	or	a, c
	jr	z, fv_done
	push	bc			; n
	srl	b
	rr	c			; bc = half
	push	hl			; p
	add	hl, bc
	add	hl, bc
	inc	hl
	ld	a, (hl)			; p[half], high byte
	cp	a, d
	jr	c, fv_less
	jr	nz, fv_notless
	dec	hl
	ld	a, (hl)
	cp	a, e
	jr	c, fv_less
fv_notless:
	pop	hl			; p
	pop	af			; n = half
	jr	fv_loop
fv_less:
	pop	hl
	add	hl, bc
	add	hl, bc
	inc	hl
	inc	hl			; p += half + 1
	ex	(sp), hl		; hl = n
	scf
	sbc	hl, bc			; n -= half + 1
	push	hl
	pop	bc
	pop	hl
	jr	fv_loop
fv_done:
	; index (bytes) = p - ids
	pop	bc			; ids
	push	bc
	push	hl
	or	a, a
	sbc	hl, bc
	ex	(sp), hl		; [sp] = 2 * index, hl = p
	; *p == id? (p may be one past the end: checked next)
	ld	a, (hl)
	cp	a, e
	jr	nz, fv_nf1
	inc	hl
	ld	a, (hl)
	cp	a, d
	jr	nz, fv_nf1
	; 2 * index < 2 * nvars?
	ld	bc, (fv_n)
	or	a, a
	sbc	hl, hl
	add	hl, bc
	add	hl, bc
	pop	de			; 2 * index
	or	a, a
	sbc	hl, de
	jr	z, fv_nf2
	jr	c, fv_nf2
	; vals + 5 * index = vals + 2 * (2 * index) + index
	ex	de, hl			; hl = 2 * index
	push	hl
	pop	de
	add	hl, hl
	srl	d
	rr	e			; de = index
	add	hl, de
	pop	de			; ids
	pop	de			; vals
	add	hl, de
	scf
	ret
fv_nf1:
	pop	de			; 2 * index
fv_nf2:
	pop	de			; ids
	pop	de			; vals
	or	a, a
	ld	a, 0xff
	ret

; PUSH_VAR op scope id: a plain read
f_push_var:
	ld	a, (iy + 1)
	cp	a, SC_LOCAL + 1
	jp	nc, slow		; arrays, instances, the stack: C
	call	room
	jp	nc, slow
	call	var_addr
	jr	c, pv_have
	inc	a
	jp	nz, slow
	; an instance variable the instance doesn't have reads as 0
	or	a, a
	sbc	hl, hl
	ld	(ix + 0), VT_REAL
	ld	(ix + 1), hl
	ld	(ix + 4), l
	jr	pv_pushed
pv_have:
	ld	a, (hl)
	cp	a, VT_HSTR
	jr	c, pv_copy
	; a string or array: one more reference (u16 refs at its start)
	push	hl
	inc	hl
	ld	hl, (hl)
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	bc
	ld	(hl), b
	dec	hl
	ld	(hl), c
	pop	hl
pv_copy:
	lea	de, ix + 0
	ld	bc, 5
	ldir
pv_pushed:
	lea	ix, ix + 5
	lea	iy, iy + 4
	jp	next

; POP_VAR op scope id: a plain write over a value with no references
f_pop_var:
	ld	a, (iy + 1)
	cp	a, SC_LOCAL + 1
	jp	nc, slow
	call	var_addr
	jp	nc, slow		; includes creating a variable
	ld	a, (hl)
	cp	a, VT_HSTR
	jp	nc, slow		; the old value needs releasing
	ex	de, hl
	lea	hl, ix - 5
	ld	bc, 5
	ldir
	lea	ix, ix - 5
	lea	iy, iy + 4
	jp	next

; ---- stack ----
f_popz:
	ld	a, (ix - 5)
	cp	a, VT_HSTR
	jp	nc, slow
	lea	ix, ix - 5
	inc	iy
	jp	next

f_dup:
	ld	a, (iy + 1)
	or	a, a
	jp	nz, slow		; more than one value
	ld	a, (ix - 5)
	cp	a, VT_HSTR
	jp	nc, slow
	call	room
	jp	nc, slow
	lea	hl, ix - 5
	lea	de, ix + 0
	ld	bc, 5
	ldir
	lea	ix, ix + 5
	lea	iy, iy + 2
	jp	next

; ---- arithmetic on two numbers ----

; carry set if the top two values are both numbers
two_reals:
	ld	a, (ix - 5)
	cp	a, VT_REAL
	jr	nz, tr_no
	ld	a, (ix - 10)
	cp	a, VT_REAL
	jr	nz, tr_no
	scf
	ret
tr_no:
	or	a, a
	ret

f_add:
	ld	hl, __fadd
	jr	arith
f_sub:
	ld	hl, __fsub
	jr	arith
f_mul:
	ld	hl, __fmul
	jr	arith
f_div:
	; dividing by zero (either sign) is an error: C
	ld	a, (ix - 1)
	and	a, 0x7f
	or	a, (ix - 2)
	or	a, (ix - 3)
	or	a, (ix - 4)
	jp	z, slow
	ld	hl, __fdiv
arith:
	ld	(arith_fn), hl
	call	two_reals
	jp	nc, slow
	push	iy
	ld	iy, (arith_fn)
	ld	bc, (ix - 9)
	ld	a, (ix - 6)
	ld	hl, (ix - 4)
	ld	e, (ix - 1)
	call	call_iy
	pop	iy
	ld	(ix - 9), bc
	ld	(ix - 6), a
	lea	ix, ix - 5
	inc	iy
	jp	next
call_iy:
	jp	(iy)

f_neg:
	ld	a, (ix - 5)
	cp	a, VT_REAL
	jp	nz, slow
	ld	a, (ix - 1)
	xor	a, 0x80
	ld	(ix - 1), a
	inc	iy
	jp	next

; GameMaker truth for the top number: > 0.5, as bits > 0x3F000000
; (signed). Carry set if true.
truthy:
	ld	a, (ix - 1)
	bit	7, a
	jr	nz, tru_no
	cp	a, 0x3f
	jr	c, tru_no
	jr	nz, tru_yes
	ld	a, (ix - 2)
	or	a, (ix - 3)
	or	a, (ix - 4)
	jr	z, tru_no
tru_yes:
	scf
	ret
tru_no:
	or	a, a
	ret

f_not:
	ld	a, (ix - 5)
	cp	a, VT_REAL
	jp	nz, slow
	call	truthy
	ld	hl, 0
	ld	a, 0
	jr	c, not_set
	ld	hl, 0x800000
	ld	a, 0x3f			; 1.0
not_set:
	ld	(ix - 4), hl
	ld	(ix - 1), a
	inc	iy
	jp	next

; (float)(int32_t)x: truncate toward zero by clearing the fraction bits
f_conv_int:
	ld	a, (ix - 5)
	cp	a, VT_REAL
	jp	nz, slow
	ld	a, (ix - 2)
	rla				; carry = exponent bit 0
	ld	a, (ix - 1)
	rla				; a = exponent
	cp	a, 150
	jr	nc, ci_done		; already whole
	cp	a, 127
	jr	nc, ci_mask
	; |x| < 1: +0
	or	a, a
	sbc	hl, hl
	ld	(ix - 4), hl
	ld	(ix - 1), l
	jr	ci_done
ci_mask:
	; clear the low 150 - exponent bits (1 to 23)
	ld	b, a
	ld	a, 150
	sub	a, b
	lea	hl, ix - 4
ci_bytes:
	cp	a, 8
	jr	c, ci_bits
	ld	(hl), 0
	inc	hl
	sub	a, 8
	jr	ci_bytes
ci_bits:
	or	a, a
	jr	z, ci_done
	ld	b, a
	ld	a, 0xff
ci_shift:
	add	a, a
	djnz	ci_shift
	and	a, (hl)
	ld	(hl), a
ci_done:
	inc	iy
	jp	next

; ---- comparisons ----

; Compare the top two numbers (a: second, b: top) as v_equal and
; v_compare do: A = 0 equal, 1 a < b, 2 a > b, 3 too close to call here.
fcompare:
	ld	hl, (ix - 9)
	ld	de, (ix - 4)
	or	a, a
	sbc	hl, de
	jr	nz, fc_diff
	ld	a, (ix - 6)
	cp	a, (ix - 1)
	jr	nz, fc_diff
	xor	a, a
	ret
fc_diff:
	; both around zero (|x| < 2^-13): C
	ld	a, (ix - 6)
	and	a, 0x7f
	cp	a, 0x39
	jr	nc, fc_big
	ld	a, (ix - 1)
	and	a, 0x7f
	cp	a, 0x39
	jr	nc, fc_big
	ld	a, 3
	ret
fc_big:
	ld	a, (ix - 6)
	xor	a, (ix - 1)
	jp	p, fc_same
	; opposite signs: the positive one is greater
	ld	a, (ix - 6)
	rla
	ld	a, 1
	ret	c
	inc	a
	ret
fc_same:
	; d = |a| - |b| as 32 bits (A: top byte, HL: low 24)
	ld	a, (ix - 1)
	and	a, 0x7f
	ld	c, a
	ld	a, (ix - 6)
	and	a, 0x7f
	ld	b, a
	ld	hl, (ix - 9)
	ld	de, (ix - 4)
	or	a, a
	sbc	hl, de
	ld	a, b
	sbc	a, c
	jp	m, fc_neg
	; d > 0: far apart if d > 4096
	or	a, a
	jr	nz, fc_agreater
	ld	de, 4097
	sbc	hl, de
	jr	nc, fc_agreater
	ld	a, 3
	ret
fc_neg:
	; d < 0: far apart if d < -4096
	inc	a
	jr	nz, fc_alesser
	ld	de, 0xfff000
	or	a, a
	sbc	hl, de
	jr	c, fc_alesser
	ld	a, 3
	ret
fc_agreater:
	; |a| > |b|: a > b if they're positive
	ld	a, (ix - 1)
	rla
	ld	a, 2
	ret	nc
	dec	a
	ret
fc_alesser:
	ld	a, (ix - 1)
	rla
	ld	a, 1
	ret	nc
	inc	a
	ret

f_cmp:
	call	two_reals
	jp	nc, slow
	call	fcompare
	cp	a, 3
	jp	z, slow
	; the op's mask of true results, bit (result)
	ld	c, a
	ld	a, (iy + 0)
	sub	a, OP_CMP_LT
	ld	hl, cmpmask
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	a, (hl)
	inc	c
cmp_shift:
	rrca
	dec	c
	jr	nz, cmp_shift
	; carry = that bit
	ld	hl, 0
	ld	a, 0
	jr	nc, cmp_set
	ld	hl, 0x800000
	ld	a, 0x3f			; 1.0
cmp_set:
	ld	(ix - 9), hl
	ld	(ix - 6), a
	lea	ix, ix - 5
	inc	iy
	jp	next

; ---- branches ----

; HL = the address of the branch target (u24 at IY + 1) and carry set if
; it's in the current code block; carry clear if not.
target:
	ld	hl, (iy + 1)
	ld	a, h
	and	a, 0xfc
	ld	h, a
	ld	l, 0
	ld	de, (blk_start)
	or	a, a
	sbc	hl, de
	jr	nz, tg_far
	ld	de, 0
	ld	a, (iy + 2)
	and	a, 3
	ld	d, a
	ld	e, (iy + 1)
	ld	hl, (_vm_base)
	add	hl, de
	scf
	ret
tg_far:
	or	a, a
	ret

f_jmp:
	call	target
	jp	nc, slow
	push	hl
	pop	iy
	jp	next

f_bt:
	ld	b, 1
	jr	branch
f_bf:
	ld	b, 0
branch:
	ld	a, (ix - 5)
	cp	a, VT_HSTR
	jp	nc, slow		; needs releasing
	ld	c, 0
	cp	a, VT_REAL
	jr	nz, br_cond		; undefined, constant string: false
	push	bc
	call	truthy
	pop	bc
	jr	nc, br_cond
	inc	c
br_cond:
	ld	a, c
	cp	a, b
	jr	z, br_take
	lea	ix, ix - 5
	lea	iy, iy + 4
	jp	next
br_take:
	call	target
	jp	nc, slow
	lea	ix, ix - 5
	push	hl
	pop	iy
	jp	next

	.size	_vm_fast, . - _vm_fast

	.section	.rodata._vm_fast_table,"a",@progbits
; bit 0: equal, bit 1: less, bit 2: greater, for LT LE EQ NE GE GT
cmpmask:
	.db	2, 3, 1, 6, 5, 4
optable:
	.d24	f_nop		; 0 NOP
	.d24	slow		; 1 PUSH_I16
	.d24	slow		; 2 PUSH_I32
	.d24	f_push_real	; 3
	.d24	slow		; 4 PUSH_STR
	.d24	f_push_var	; 5
	.d24	f_pop_var	; 6
	.d24	f_popz		; 7
	.d24	f_dup		; 8
	.d24	f_add		; 9
	.d24	f_sub		; 10
	.d24	f_mul		; 11
	.d24	f_div		; 12
	.d24	slow		; 13 REM
	.d24	slow		; 14 MOD
	.d24	slow		; 15 AND
	.d24	slow		; 16 OR
	.d24	slow		; 17 XOR
	.d24	slow		; 18 SHL
	.d24	slow		; 19 SHR
	.d24	f_cmp		; 20 CMP_LT
	.d24	f_cmp		; 21 CMP_LE
	.d24	f_cmp		; 22 CMP_EQ
	.d24	f_cmp		; 23 CMP_NE
	.d24	f_cmp		; 24 CMP_GE
	.d24	f_cmp		; 25 CMP_GT
	.d24	f_neg		; 26
	.d24	f_not		; 27
	.d24	f_jmp		; 28
	.d24	f_bt		; 29
	.d24	f_bf		; 30
	.d24	slow		; 31 PUSHENV
	.d24	slow		; 32 POPENV
	.d24	slow		; 33 POPENV_DROP
	.d24	slow		; 34 CALL
	.d24	slow		; 35 CALL_SCRIPT
	.d24	slow		; 36 RET
	.d24	slow		; 37 EXIT
	.d24	f_conv_int	; 38
	.d24	f_push_undef	; 39

	.section	.bss._vm_fast_vars,"aw",@nobits
blk_start:
	.skip	3
arith_fn:
	.skip	3
fv_n:
	.skip	3
