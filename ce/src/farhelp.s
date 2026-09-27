; Helpers for code that runs from the archive (tools/farcode.py). Far code
; can sit anywhere in flash, so its jumps and calls to its own code are
; relative: a call to one of these, then the 24-bit distance from the end
; of that field to the target.
;
;   call __pic_jp / .d24 off              jump
;   call cc, __pic_jpc / jr +3 / .d24 off  conditional jump
;   call __pic_call / .d24 off             call (returns after the field)
;   call cc, __pic_callc / jr +3 / .d24    conditional call
;   call __pic_lea / .d24 off              hl = address
;
; Every register and the flags keep their values (calls don't need the
; flags), so the compiler's code around them works unchanged.

	.assume	ADL = 1

	.section	.text.__pic_jp,"ax",@progbits
	.globl	__pic_jp
__pic_jpc:
	ex	(sp), hl		; hl = the jr before the field
	inc	hl
	inc	hl
	jr	pic_jump
	.globl	__pic_jpc
__pic_jp:
	ex	(sp), hl		; hl = field, stack: saved hl
pic_jump:
	push	de
	push	af
	ld	de, (hl)
	inc	hl
	inc	hl
	inc	hl
	add	hl, de
	pop	af
	pop	de
	ex	(sp), hl		; stack: target
	ret

	.section	.text.__pic_call,"ax",@progbits
	.globl	__pic_call
	.globl	__pic_callc
__pic_callc:
	ex	(sp), hl
	inc	hl
	inc	hl
	jr	pic_callx
__pic_call:
	ex	(sp), hl		; hl = field, stack: saved hl
pic_callx:
	ld	(pic_de), de
	ld	de, (hl)
	inc	hl
	inc	hl
	inc	hl			; hl = return address
	ex	(sp), hl		; stack: return address
	ld	(pic_hl), hl
	pop	hl
	push	hl
	add	hl, de
	push	hl			; stack: target, return address
	ld	hl, (pic_hl)
	ld	de, (pic_de)
	ret

	.section	.text.__pic_lea,"ax",@progbits
	.globl	__pic_lea
__pic_lea:
	ld	(pic_de), de
	ex	(sp), hl		; hl = field
	push	af
	ld	de, (hl)
	inc	hl
	inc	hl
	inc	hl
	push	hl			; stack: end, af, x
	add	hl, de
	ld	(pic_hl), hl
	pop	hl
	pop	af
	ex	(sp), hl		; stack: end (the return address)
	ld	hl, (pic_hl)
	ld	de, (pic_de)
	ret

	.section	.bss.pic_scratch,"aw",@nobits
pic_hl:
	.ds	3
pic_de:
	.ds	3
