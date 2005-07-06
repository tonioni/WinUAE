_TEXT SEGMENT 

_stack_magic_amd64:
	mov rax, [rsp+16]   ; move parameter 'pc' to a spare register
	mov rsp, r9        ; swap stack
	mov rsp, r9        ; swap stack
	call rax         ; call function pointed to by 'pc'

	mov rax,rsp
	sub rax,2300*512
	ret
	ret

END
