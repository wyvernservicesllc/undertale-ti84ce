; void run_on_stack(void *stack_top, void (*fn)(void));
;
; Calls fn with the stack pointer moved to stack_top, then restores it.
; The OS stack is only about 4 KB, too little for the VM's nested calls.

	.assume	ADL = 1

	.section	.text._run_on_stack,"ax",@progbits
	.globl	_run_on_stack
	.type	_run_on_stack,@function
_run_on_stack:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)		; new stack top
	ld	de, (iy + 6)		; fn
	ld	(saved_sp), sp
	ld	sp, hl
	ex	de, hl
	ld	bc, .Lreturn
	push	bc
	jp	(hl)
.Lreturn:
	ld	sp, (saved_sp)
	ret
	.size	_run_on_stack, . - _run_on_stack

	.section	.data._saved_sp,"aw",@progbits
saved_sp:
	.d24	0
