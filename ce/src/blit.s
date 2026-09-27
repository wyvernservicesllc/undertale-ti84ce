; Inner loops of the renderer (render_ce.c) that are too slow in C.
; Some patch their own code (steps, sizes): the program runs from RAM.

	.assume	ADL = 1

; void dither_fill(uint8_t *dst, uint8_t n, uint8_t color);
;
; n pixels of color at dst, dst + 2, dst + 4... (a dithered span)

	.section	.text._dither_fill,"ax",@progbits
	.globl	_dither_fill
	.type	_dither_fill,@function
_dither_fill:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)
	ld	b, (iy + 6)
	ld	a, (iy + 9)
	inc	b
	dec	b
	ret	z
.Ldf:
	ld	(hl), a
	inc	hl
	inc	hl
	djnz	.Ldf
	ret
	.size	_dither_fill, . - _dither_fill

; void blit_tab(uint8_t *dst, uint8_t *const *tab, unsigned n, const uint8_t *lut, uint8_t step);
;
; For k < n: dst[k * step] = lut[*tab[k * step]], leaving transparent (0)
; pixels alone. lut is NULL or 256-byte aligned. step 2 draws every other
; pixel (dithered translucency).

	.section	.text._blit_tab,"ax",@progbits
	.globl	_blit_tab
	.type	_blit_tab,@function
_blit_tab:
	ld	iy, 0
	add	iy, sp
	push	ix
	ld	a, (iy + 15)		; step
	ld	(.Lbt_ds1 + 2), a
	ld	(.Lbt_ds2 + 2), a
	ld	c, a
	add	a, a
	add	a, c
	ld	(.Lbt_ts1 + 2), a	; 3 * step
	ld	(.Lbt_ts2 + 2), a
	ld	ix, (iy + 6)		; tab
	ld	bc, (iy + 9)		; n
	ld	de, (iy + 12)		; lut
	ld	iy, (iy + 3)		; dst
	ld	hl, 0
	or	a, a
	sbc	hl, de
	jr	nz, .Lbt_lut
.Lbt_loop:				; no lut
	ld	a, b
	or	a, c
	jr	z, .Lbt_done
	dec	bc
	ld	hl, (ix)
.Lbt_ts1:
	lea	ix, ix + 3
	ld	a, (hl)
	or	a, a
	jr	z, .Lbt_ds1
	ld	(iy), a
.Lbt_ds1:
	lea	iy, iy + 1
	jr	.Lbt_loop
.Lbt_lut:
	ld	a, b
	or	a, c
	jr	z, .Lbt_done
	dec	bc
	ld	hl, (ix)
.Lbt_ts2:
	lea	ix, ix + 3
	ld	a, (hl)
	or	a, a
	jr	z, .Lbt_ds2
	ld	e, a			; de: the lut, 256-aligned
	ld	a, (de)
	ld	(iy), a
.Lbt_ds2:
	lea	iy, iy + 1
	jr	.Lbt_lut
.Lbt_done:
	pop	ix
	ret
	.size	_blit_tab, . - _blit_tab

; void rot_span(uint8_t *dst, uint8_t n, unsigned u, unsigned v, unsigned du, unsigned dv,
;               const uint8_t *row0, uint8_t step);
;
; One row of a rotated image: n pixels at dst, step apart. u, v: the source
; position of the first, in 8.16 fixed point (24 bits, the integer part
; wraps at 256), stepped by du, dv per pixel. The source rows are 256 bytes
; apart from row0 (256-aligned, all in one 64 KB bank), zero (transparent)
; past the image's width and in a row above and below it, so positions a
; pixel off its edges read nothing. The integer position is the address:
; IXL = column, IXH = row; the fractions carry into them.

	.section	.text._rot_span,"ax",@progbits
	.globl	_rot_span
	.type	_rot_span,@function
_rot_span:
	ld	iy, 0
	add	iy, sp
	push	ix
	ld	a, (iy + 24)		; step
	ld	(.Lrs_step + 2), a
	ld	ix, (iy + 21)		; row0
	ld	a, (iy + 11)		; the column
	ld	ixl, a
	ld	a, (iy + 14)		; the row
	add	a, ixh
	ld	ixh, a
	ld	b, (iy + 6)		; n
	ld	hl, (iy + 9)		; u (the fraction: the low 16 bits)
	ld	de, (iy + 15)
	ld	c, (iy + 17)		; du's integer part
	exx
	ld	hl, (iy + 12)		; v
	ld	de, (iy + 18)
	ld	c, (iy + 20)
	exx
	ld	iy, (iy + 3)		; dst
	inc	b
	dec	b
	jr	z, .Lrs_done
.Lrs_loop:
	ld	a, (ix)
	or	a, a
	jr	z, .Lrs_step
	ld	(iy), a
.Lrs_step:
	lea	iy, iy + 1
	add.s	hl, de
	ld	a, ixl
	adc	a, c
	ld	ixl, a
	exx
	add.s	hl, de
	ld	a, ixh
	adc	a, c
	ld	ixh, a
	exx
	djnz	.Lrs_loop
.Lrs_done:
	pop	ix
	ret
	.size	_rot_span, . - _rot_span
