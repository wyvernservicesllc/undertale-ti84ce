; __fadd, __fsub, __fmul: IEEE single precision add, subtract and multiply, correctly
; rounded (to nearest, ties to even), for the C compiler's float operations.
; They replace the boot ROM's routines, which run from flash and take
; ~3400 cycles; these run from RAM. They also round exactly like the Mac,
; where the boot ROM's differ in the last bit now and then.
;
; In: x in A:UBC (A = bits 31-24), y in E:UHL. Out: A:UBC.
; Preserves every register but AF and BC, as the compiler expects.

	.assume	ADL = 1

	.section	.text.__fadd,"ax",@progbits
	.globl	__fadd
	.globl	__fsub
	.type	__fadd,@function
	.type	__fsub,@function

__fsub:
	push	ix
	push	de
	push	hl
	ld	ix, fscratch
	ld	(ix + 0), bc
	ld	(ix + 3), a
	ld	(ix + 4), hl
	ld	a, e
	xor	a, 0x80			; x - y = x + (-y)
	ld	(ix + 7), a
	jr	fadd_common

__fadd:
	push	ix
	push	de
	push	hl
	ld	ix, fscratch
	ld	(ix + 0), bc
	ld	(ix + 3), a
	ld	(ix + 4), hl
	ld	(ix + 7), e

; scratch: 0-3 x, 4-7 y (little endian floats), 8-11 x's mantissa << 7,
; 12 x's exponent, 13 x's sign, 14 y's exponent, 15 y's sign, 16-18 result
fadd_common:
	; the larger magnitude goes in x
	ld	a, (ix + 3)
	and	a, 0x7f
	ld	c, a
	ld	a, (ix + 7)
	and	a, 0x7f
	cp	a, c
	jr	c, .Lnoswap
	jr	nz, .Lswap
	ld	a, (ix + 6)
	cp	a, (ix + 2)
	jr	c, .Lnoswap
	jr	nz, .Lswap
	ld	a, (ix + 5)
	cp	a, (ix + 1)
	jr	c, .Lnoswap
	jr	nz, .Lswap
	ld	a, (ix + 4)
	cp	a, (ix + 0)
	jr	c, .Lnoswap
	jr	z, .Lnoswap
.Lswap:
	ld	hl, (ix + 0)
	ld	de, (ix + 4)
	ld	(ix + 0), de
	ld	(ix + 4), hl
	ld	a, (ix + 3)
	ld	c, (ix + 7)
	ld	(ix + 3), c
	ld	(ix + 7), a
.Lnoswap:
	; exponents and signs
	ld	a, (ix + 2)
	rla
	ld	a, (ix + 3)
	rla
	ld	(ix + 12), a
	ld	a, (ix + 3)
	and	a, 0x80
	ld	(ix + 13), a
	ld	a, (ix + 6)
	rla
	ld	a, (ix + 7)
	rla
	ld	(ix + 14), a
	ld	a, (ix + 7)
	and	a, 0x80
	ld	(ix + 15), a

	; infinities and NaNs (x has the largest magnitude, so any NaN)
	ld	a, (ix + 12)
	inc	a
	jr	nz, .Lfinite
	ld	a, (ix + 2)
	and	a, 0x7f
	or	a, (ix + 1)
	or	a, (ix + 0)
	jr	nz, .Lnan_x
	ld	a, (ix + 14)
	inc	a
	jr	nz, .Lret_x
	ld	a, (ix + 13)
	xor	a, (ix + 15)
	jr	z, .Lret_x
	ld	bc, 0xc00000		; inf - inf: the default NaN
	ld	a, 0x7f
	jp	.Ldone
.Lnan_x:
	set	6, (ix + 2)		; quiet
.Lret_x:
	ld	bc, (ix + 0)
	ld	a, (ix + 3)
	jp	.Ldone

.Lfinite:
	; x's mantissa (with the implicit 1) << 7 at 8-11
	xor	a, a
	ld	(ix + 8), a
	ld	a, (ix + 0)
	ld	(ix + 9), a
	ld	a, (ix + 1)
	ld	(ix + 10), a
	ld	a, (ix + 2)
	and	a, 0x7f
	ld	c, a
	ld	a, (ix + 12)
	or	a, a
	jr	z, .Lxden
	set	7, c
	jr	.Lxok
.Lxden:
	inc	(ix + 12)		; denormal: exponent 1, no implicit 1
.Lxok:
	ld	(ix + 11), c
	srl	(ix + 11)
	rr	(ix + 10)
	rr	(ix + 9)
	rr	(ix + 8)

	; y's the same way, in D:E:H:L (8-bit registers)
	ld	a, (ix + 6)
	and	a, 0x7f
	ld	d, a
	ld	a, (ix + 14)
	or	a, a
	jr	z, .Lyden
	set	7, d
	jr	.Lyok
.Lyden:
	inc	(ix + 14)
.Lyok:
	ld	e, (ix + 5)
	ld	h, (ix + 4)
	ld	l, 0
	srl	d
	rr	e
	rr	h
	rr	l

	; line y up with x: shift right by the exponent difference, keeping
	; whether anything fell off (sticky) in the lowest bit
	ld	a, (ix + 12)
	sub	a, (ix + 14)
	ld	c, 0
	cp	a, 32
	jr	c, .Lbytes
	ld	a, d
	or	a, e
	or	a, h
	or	a, l
	ld	d, 0
	ld	e, 0
	ld	h, 0
	ld	l, 0
	jr	z, .Laligned
	inc	l
	jr	.Laligned
.Lbytes:
	cp	a, 8
	jr	c, .Lbits
	ld	b, a
	ld	a, l
	or	a, c
	ld	c, a
	ld	l, h
	ld	h, e
	ld	e, d
	ld	d, 0
	ld	a, b
	sub	a, 8
	jr	.Lbytes
.Lbits:
	or	a, a
	jr	z, .Ljam
	ld	b, a
.Lbitloop:
	srl	d
	rr	e
	rr	h
	rr	l
	jr	nc, .Lnobit
	ld	c, 1
.Lnobit:
	djnz	.Lbitloop
.Ljam:
	ld	a, c
	or	a, a
	jr	z, .Laligned
	set	0, l
.Laligned:

	ld	a, (ix + 13)
	xor	a, (ix + 15)
	jr	nz, .Lsub

	; same signs: add
	ld	a, l
	add	a, (ix + 8)
	ld	l, a
	ld	a, h
	adc	a, (ix + 9)
	ld	h, a
	ld	a, e
	adc	a, (ix + 10)
	ld	e, a
	ld	a, d
	adc	a, (ix + 11)
	ld	d, a
	bit	7, d
	jr	z, .Lnorm
	srl	d			; carried into bit 31: one bit right
	rr	e
	rr	h
	rr	l
	jr	nc, .Lnoj
	set	0, l
.Lnoj:
	inc	(ix + 12)
	jr	.Lround

.Lsub:
	; opposite signs: x - y (x is the larger)
	ld	a, (ix + 8)
	sub	a, l
	ld	l, a
	ld	a, (ix + 9)
	sbc	a, h
	ld	h, a
	ld	a, (ix + 10)
	sbc	a, e
	ld	e, a
	ld	a, (ix + 11)
	sbc	a, d
	ld	d, a
	or	a, e
	or	a, h
	or	a, l
	jr	nz, .Lnorm
	xor	a, a			; exactly zero: +0
	ld	bc, 0
	jr	.Ldone

.Lnorm:
	; bring the leading 1 to bit 30 (not below exponent 1: denormals)
	bit	6, d
	jr	nz, .Lround
	ld	a, (ix + 12)
	cp	a, 2
	jr	c, .Lround
	dec	(ix + 12)
	sla	l
	rl	h
	rl	e
	rl	d
	jr	.Lnorm

.Lround:
	; bits 6-0 of L are below the last mantissa bit (bit 7)
	ld	a, l
	and	a, 0x7f
	cp	a, 0x40
	jr	c, .Lpack
	jr	nz, .Lup
	bit	7, l			; a tie: to even
	jr	z, .Lpack
.Lup:
	ld	a, l
	add	a, 0x80
	ld	l, a
	jr	nc, .Lpack
	inc	h
	jr	nz, .Lpack
	inc	e
	jr	nz, .Lpack
	inc	d
	bit	7, d
	jr	z, .Lpack
	srl	d
	rr	e
	rr	h
	rr	l
	inc	(ix + 12)

.Lpack:
	ld	a, (ix + 12)
	inc	a
	jr	z, .Linf
	sla	l			; the 24-bit mantissa now in D:E:H
	rl	h
	rl	e
	rl	d
	ld	b, (ix + 12)
	bit	7, d
	jr	nz, .Lexpok
	ld	b, 0			; no leading 1: denormal
.Lexpok:
	ld	a, d
	and	a, 0x7f
	bit	0, b
	jr	z, .Leven
	or	a, 0x80
.Leven:
	ld	(ix + 18), a
	ld	(ix + 17), e
	ld	(ix + 16), h
	ld	a, b
	srl	a
	or	a, (ix + 13)
	ld	bc, (ix + 16)
	jr	.Ldone
.Linf:
	ld	bc, 0x800000
	ld	a, (ix + 13)
	or	a, 0x7f
.Ldone:
	pop	hl
	pop	de
	pop	ix
	ret

	.size	__fadd, . - __fadd

	.section	.bss.fscratch,"aw",@nobits
fscratch:
	.skip	19

; __fmul: IEEE single precision multiply, correctly rounded. Same
; conventions as __fadd. The 24 x 24 bit mantissa product is summed column
; by column from the eZ80's 8 x 8 bit MLT.

	.section	.text.__fmul,"ax",@progbits
	.globl	__fmul
	.type	__fmul,@function

; scratch: 0-3 x, 4-7 y, 8-13 product (little endian), 14 sign,
; 15-17 x's exponent (signed 24-bit), 18-20 y's, 21 guard, 22 sticky
__fmul:
	push	ix
	push	de
	push	hl
	ld	ix, mscratch
	ld	(ix + 0), bc
	ld	(ix + 3), a
	ld	(ix + 4), hl
	ld	(ix + 7), e
	xor	a, e
	and	a, 0x80
	ld	(ix + 14), a		; sign of the product

	; exponents (raw)
	ld	a, (ix + 2)
	rla
	ld	a, (ix + 3)
	rla
	ld	hl, 0
	ld	l, a
	ld	(ix + 15), hl
	ld	a, (ix + 6)
	rla
	ld	a, (ix + 7)
	rla
	ld	hl, 0
	ld	l, a
	ld	(ix + 18), hl

	; NaN and infinity
	ld	a, (ix + 15)
	inc	a
	jr	z, .Lmspecial
	ld	a, (ix + 18)
	inc	a
	jr	z, .Lmspecial
	jr	.Lmfinite
.Lmspecial:
	; a NaN operand: return it quieted (x's first)
	ld	a, (ix + 15)
	inc	a
	jr	nz, .Lmxnotnan
	ld	a, (ix + 2)
	and	a, 0x7f
	or	a, (ix + 1)
	or	a, (ix + 0)
	jr	z, .Lmxnotnan
	set	6, (ix + 2)
	ld	bc, (ix + 0)
	ld	a, (ix + 3)
	jp	.Lmdone
.Lmxnotnan:
	ld	a, (ix + 18)
	inc	a
	jr	nz, .Lmynotnan
	ld	a, (ix + 6)
	and	a, 0x7f
	or	a, (ix + 5)
	or	a, (ix + 4)
	jr	z, .Lmynotnan
	set	6, (ix + 6)
	ld	bc, (ix + 4)
	ld	a, (ix + 7)
	jp	.Lmdone
.Lmynotnan:
	; infinity times zero is NaN, otherwise infinity
	ld	a, (ix + 3)
	and	a, 0x7f
	or	a, (ix + 2)
	or	a, (ix + 1)
	or	a, (ix + 0)
	jr	z, .Lmnan
	ld	a, (ix + 7)
	and	a, 0x7f
	or	a, (ix + 6)
	or	a, (ix + 5)
	or	a, (ix + 4)
	jr	z, .Lmnan
	jp	.Lminf
.Lmnan:
	ld	bc, 0xc00000
	ld	a, 0x7f
	jp	.Lmdone

.Lmfinite:
	; zero times anything is a signed zero
	ld	a, (ix + 3)
	and	a, 0x7f
	or	a, (ix + 2)
	or	a, (ix + 1)
	or	a, (ix + 0)
	jp	z, .Lmzero
	ld	a, (ix + 7)
	and	a, 0x7f
	or	a, (ix + 6)
	or	a, (ix + 5)
	or	a, (ix + 4)
	jp	z, .Lmzero

	; mantissas with the implicit 1; denormals normalized (exponent
	; going below 1)
	ld	a, (ix + 2)
	and	a, 0x7f
	ld	(ix + 2), a
	ld	a, (ix + 15)
	or	a, a
	jr	z, .Lmxden
	set	7, (ix + 2)
	jr	.Lmxok
.Lmxden:
	ld	hl, 1
	ld	(ix + 15), hl
.Lmxnorm:
	bit	7, (ix + 2)
	jr	nz, .Lmxok
	sla	(ix + 0)
	rl	(ix + 1)
	rl	(ix + 2)
	ld	hl, (ix + 15)
	dec	hl
	ld	(ix + 15), hl
	jr	.Lmxnorm
.Lmxok:
	ld	a, (ix + 6)
	and	a, 0x7f
	ld	(ix + 6), a
	ld	a, (ix + 18)
	or	a, a
	jr	z, .Lmyden
	set	7, (ix + 6)
	jr	.Lmyok
.Lmyden:
	ld	hl, 1
	ld	(ix + 18), hl
.Lmynorm:
	bit	7, (ix + 6)
	jr	nz, .Lmyok
	sla	(ix + 4)
	rl	(ix + 5)
	rl	(ix + 6)
	ld	hl, (ix + 18)
	dec	hl
	ld	(ix + 18), hl
	jr	.Lmynorm
.Lmyok:

	; the product, a column at a time: HL sums the column's byte
	; products plus the carry of the one before
	ld	hl, 0
	; column 0: x0 y0
	ld	bc, 0
	ld	b, (ix + 0)
	ld	c, (ix + 4)
	mlt	bc
	add	hl, bc
	ld	(ix + 8), l
	call	.Lmshift
	; column 1: x0 y1, x1 y0
	ld	bc, 0
	ld	b, (ix + 0)
	ld	c, (ix + 5)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (ix + 1)
	ld	c, (ix + 4)
	mlt	bc
	add	hl, bc
	ld	(ix + 9), l
	call	.Lmshift
	; column 2: x0 y2, x1 y1, x2 y0
	ld	bc, 0
	ld	b, (ix + 0)
	ld	c, (ix + 6)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (ix + 1)
	ld	c, (ix + 5)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (ix + 2)
	ld	c, (ix + 4)
	mlt	bc
	add	hl, bc
	ld	(ix + 10), l
	call	.Lmshift
	; column 3: x1 y2, x2 y1
	ld	bc, 0
	ld	b, (ix + 1)
	ld	c, (ix + 6)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (ix + 2)
	ld	c, (ix + 5)
	mlt	bc
	add	hl, bc
	ld	(ix + 11), l
	call	.Lmshift
	; column 4: x2 y2
	ld	bc, 0
	ld	b, (ix + 2)
	ld	c, (ix + 6)
	mlt	bc
	add	hl, bc
	ld	(ix + 12), l
	ld	(ix + 13), h

	; exponent: ex + ey - 127, one more if the product reached bit 47
	ld	hl, (ix + 15)
	ld	de, (ix + 18)
	add	hl, de
	ld	de, -127
	add	hl, de
	bit	7, (ix + 13)
	jr	z, .Lmsmall
	inc	hl
	jr	.Lmexp
.Lmsmall:
	sla	(ix + 8)		; bring the leading 1 to bit 47
	rl	(ix + 9)
	rl	(ix + 10)
	rl	(ix + 11)
	rl	(ix + 12)
	rl	(ix + 13)
.Lmexp:
	; mantissa: bytes 11-13; guard: bit 7 of byte 10; sticky: the rest
	ld	a, (ix + 10)
	and	a, 0x80
	ld	(ix + 21), a
	ld	a, (ix + 10)
	and	a, 0x7f
	or	a, (ix + 9)
	or	a, (ix + 8)
	ld	(ix + 22), a

	; exponent in HL (signed): too big, normal or denormal
	push	hl
	ld	de, 255
	or	a, a
	sbc	hl, de
	pop	hl
	jp	p, .Lminf		; >= 255 (HL fits in 24 bits, no overflow)
	; >= 1: normal
	push	hl
	dec	hl
	add	hl, hl			; sign of (hl - 1) to carry
	pop	hl
	jr	nc, .Lmround
	; <= 0: shift right 1 - e bits (with guard and sticky), exponent 0
	ex	de, hl
	ld	hl, 1
	or	a, a
	sbc	hl, de			; 1 - e, >= 1
	ld	de, 26
	or	a, a
	sbc	hl, de
	jr	c, .Lmfew
	ld	hl, 26			; anything more ends up all sticky
	jr	.Lmcount
.Lmfew:
	add	hl, de
.Lmcount:
	ld	b, l
.Lmdenloop:
	ld	a, (ix + 21)		; sticky |= guard
	or	a, (ix + 22)
	ld	(ix + 22), a
	xor	a, a
	srl	(ix + 13)
	rr	(ix + 12)
	rr	(ix + 11)
	rra				; the bit shifted out is the new guard
	ld	(ix + 21), a
	djnz	.Lmdenloop
	ld	hl, 0

.Lmround:
	; HL = exponent (0 for denormals); round to nearest even
	ld	a, (ix + 21)
	or	a, a
	jr	z, .Lmpack
	ld	a, (ix + 22)
	or	a, a
	jr	nz, .Lmup
	bit	0, (ix + 11)
	jr	z, .Lmpack
.Lmup:
	inc	(ix + 11)
	jr	nz, .Lmcarried
	inc	(ix + 12)
	jr	nz, .Lmcarried
	inc	(ix + 13)
	jr	nz, .Lmcarried
	; 0xffffff + 1: mantissa 0x800000, exponent + 1
	ld	(ix + 13), 0x80
	inc	hl
	jr	.Lmcarried2
.Lmcarried:
	; a denormal that rounded up into the normal range
	ld	a, l
	or	a, a
	jr	nz, .Lmcarried2
	bit	7, (ix + 13)
	jr	z, .Lmcarried2
	inc	l
.Lmcarried2:
	ld	de, 255
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	jr	nc, .Lminf

.Lmpack:
	; HL = exponent 0-254, mantissa (with the leading 1 unless denormal)
	; in bytes 11-13
	ld	a, (ix + 13)
	and	a, 0x7f
	bit	0, l
	jr	z, .Lmeven
	or	a, 0x80
.Lmeven:
	ld	(ix + 13), a
	ld	a, l
	srl	a
	or	a, (ix + 14)
	ld	bc, (ix + 11)
	jr	.Lmdone
.Lmzero:
	ld	bc, 0
	ld	a, (ix + 14)
	jr	.Lmdone
.Lminf:
	ld	bc, 0x800000
	ld	a, (ix + 14)
	or	a, 0x7f
.Lmdone:
	pop	hl
	pop	de
	pop	ix
	ret

; HL >>= 8 (the column's carry into the next)
.Lmshift:
	ld	(ix + 23), hl
	xor	a, a
	ld	(ix + 26), a
	ld	hl, (ix + 24)
	ret

	.size	__fmul, . - __fmul

	.section	.bss.mscratch,"aw",@nobits
mscratch:
	.skip	27
