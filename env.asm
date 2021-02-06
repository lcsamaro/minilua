section .data

    tag_cfunction: equ 0xfffd
    tag_lfunction: equ 0xfffd ; TODO


section .text

    extern lua_error
    extern lua_gc_impl

    global ml_indirect_call
    global ml_indirect_luacall

    global ml_get_rsp

    global lua_gc


; rcx - boxed c function
ml_indirect_call:
    ; get tag
    mov rax, rcx
    shr rax, 48
    ; check tag
    cmp ax, tag_cfunction
    jne .maybe_lfunction
    ; clear high 16 bits
    mov rax, 0xffffffffffff
    and rcx, rax
    ; set args
    mov rdx, rsp
    add rdx, 8
    ; call function
    jmp rcx
.maybe_lfunction:
    ; check tag
    cmp ax, tag_lfunction
    jne lua_error ; invalid object type
    ; clear high 16 bits
    mov rax, 0xffffffffffff
    and rcx, rax
    ; add nil params - by checking function header no. parameters

    ; call function
    jmp rcx




ml_indirect_luacall:


    ret

; arithmetic guards



; misc
ml_get_rsp:
    mov rax, rsp
    ret

lua_gc:
    push rbx ; alignment
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    call lua_gc_impl
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop rbx ; alignment
    ret


; inlined

ml_fix_arity_begin:



ml_fix_arity_end:
