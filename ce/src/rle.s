; void rle_copy(uint8_t *dst, const uint8_t *p, const uint8_t *end, unsigned count);
;
; Draws one row of an RLE image (format: tools/cepack.py, rle_row) onto
; the screen, 1:1, from the row's first pixel, for count pixels at most.
; Tokens: 0x00-0x3f: t + 1 transparent pixels; 0x40-0x7f: (t & 0x3f) + 1
; pixels of the next byte's color; 0x80-0xff: (t & 0x7f) + 1 literal
; pixels follow. render_ce.c does the other cases (clipped on the left,
; color lookup tables) in C.

	.assume	ADL = 1

	.section	.text._rle_copy,"ax",@progbits
	.globl	_rle_copy
	.type	_rle_copy,@function
_rle_copy:
	ld	iy, 0
	add	iy, sp
	ld	de, (iy + 3)		; dst
	ld	hl, (iy + 9)
	ld	(rc_end), hl
	ld	bc, (iy + 12)
	ld	(rc_left), bc		; pixels still to draw
	ld	hl, (iy + 6)		; p
.Lloop:
	; stop at the end of the row data or when count pixels are done
	push	hl
	ld	bc, (rc_end)
	or	a, a
	sbc	hl, bc
	pop	hl
	ret	nc
	ld	bc, (rc_left)
	ld	a, b
	or	a, c
	ret	z			; (count < 65536)
	ld	a, (hl)
	inc	hl
	cp	a, 0x40
	jr	c, .Lskip
	cp	a, 0x80
	jr	c, .Lfill
	; literal run
	and	a, 0x7f
	inc	a
	call	.Lclip			; bc = min(n, left), updates left
	ldir				; hl, de advance by bc
	; skip what was clipped off (only at the row's end: we stop next)
	jr	.Lloop
.Lskip:
	inc	a
	call	.Lclip
	ex	de, hl
	add	hl, bc
	ex	de, hl
	jr	.Lloop
.Lfill:
	and	a, 0x3f
	inc	a
	call	.Lclip
	ld	a, (hl)
	inc	hl
	; bc pixels of color a
	ld	(de), a
	inc	de
	dec	bc
	push	hl
	push	de
	pop	hl
	dec	hl			; hl = the pixel just written
	ld	a, b
	or	a, c
	jr	z, .Lfilled
	ldir				; copies it forward
.Lfilled:
	pop	hl
	jr	.Lloop

; A = run length (1-128): BC = min(A, left), left -= BC
.Lclip:
	push	hl
	ld	bc, 0
	ld	c, a
	ld	hl, (rc_left)
	or	a, a
	sbc	hl, bc
	jr	nc, .Lfits
	; run longer than what's left: draw only that
	ld	bc, (rc_left)
	or	a, a
	sbc	hl, hl
.Lfits:
	ld	(rc_left), hl
	pop	hl
	ret

	.size	_rle_copy, . - _rle_copy

	.section	.bss._rle_copy_vars,"aw",@nobits
rc_end:
	.skip	3
rc_left:
	.skip	3

; void glyph_blit(uint8_t *dst, const uint8_t *bits, unsigned rowb, unsigned h, uint8_t color);
;
; Draws a 1 bpp font glyph (rows of rowb bytes, MSB first, unused bits 0)
; in one color at dst on the 320-pixel-wide buffer. No clipping.

	.section	.text._glyph_blit,"ax",@progbits
	.globl	_glyph_blit
	.type	_glyph_blit,@function
_glyph_blit:
	ld	iy, 0
	add	iy, sp
	push	ix
	ld	de, (iy + 3)		; dst
	ld	hl, (iy + 6)		; bits
	ld	a, (iy + 9)
	ld	ixl, a			; bytes per row
	ld	a, (iy + 12)
	ld	ixh, a			; rows
	ld	a, (iy + 15)		; color
.Lgrow:
	push	de
	ld	b, ixl
.Lgbyte:
	ld	c, (hl)
	inc	hl
	inc	c
	dec	c
	jr	z, .Lgempty
	.rept	8
	sla	c
	jr	nc, 1f
	ld	(de), a
1:
	inc	de
	.endr
	djnz	.Lgbyte
	jr	.Lgnext
.Lgempty:
	inc	de
	inc	de
	inc	de
	inc	de
	inc	de
	inc	de
	inc	de
	inc	de
	djnz	.Lgbyte
.Lgnext:
	pop	de
	ex	de, hl
	ld	bc, 320
	add	hl, bc
	ex	de, hl
	dec	ixh
	jr	nz, .Lgrow
	pop	ix
	ret

	.size	_glyph_blit, . - _glyph_blit

; void scale_row(uint8_t *dst, uint8_t *const *tab, unsigned n);
;
; One row of a scaled image: dst[k] = *tab[k] for k < n, skipping
; transparent (0) pixels. tab points into the decoded source row.

	.section	.text._scale_row,"ax",@progbits
	.globl	_scale_row
	.type	_scale_row,@function
_scale_row:
	ld	iy, 0
	add	iy, sp
	push	ix
	ld	de, (iy + 3)
	ld	ix, (iy + 6)
	ld	bc, (iy + 9)
.Lsloop:
	ld	a, b
	or	a, c
	jr	z, .Lsdone
	dec	bc
	ld	hl, (ix)
	lea	ix, ix + 3
	ld	a, (hl)
	or	a, a
	jr	z, .Lsskip
	ld	(de), a
.Lsskip:
	inc	de
	jr	.Lsloop
.Lsdone:
	pop	ix
	ret

	.size	_scale_row, . - _scale_row
