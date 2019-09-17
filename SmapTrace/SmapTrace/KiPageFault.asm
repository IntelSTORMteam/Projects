;
;  Intel provides this code “as-is” and disclaims all express and implied warranties, including without 
;  limitation, the implied warranties of merchantability, fitness for a particular purpose, and non-infringement, 
;  as well as any warranty arising from course of performance, course of dealing, or usage in trade. No license 
;  (express or implied, by estoppel or otherwise) to any intellectual property rights is granted by Intel providing 
;  this code.
;  This code is preliminary, may contain errors and is subject to change without notice. 
;  Intel technologies' features and benefits depend on system configuration and may require enabled hardware, 
;  software or service activation. Performance varies depending on system configuration.  Any differences in your 
;  system hardware, software or configuration may affect your actual performance.  No product or component can be 
;  absolutely secure.
;  Intel and the Intel logo are trademarks of Intel Corporation in the United States and other countries. 
;  *Other names and brands may be claimed as the property of others.
;  © Intel Corporation
;

;
;  Licensed under the GPL v2
;

use64

; KiPageFault entry

; Debug or nop
nop

; Prologue
push rax
push rcx
push rdx
push r8
push r9
push r10
push r11

; Raise IRQL
mov  rax, cr8
push rax
mov  rax, 2
mov  cr8, rax

; Get error code
mov  rcx, [rsp + 0x40]
; Get fault address
mov  rdx, cr2
; Get fault rip
mov  r8, [rsp + 0x48]
; Get fault rsp
mov  r9, [rsp + 0x60]

; Check faulting address
bt   rdx, 0x38
jc   OrigRet

; Check source rip
bt   r8, 0x38
jnc  OrigRet

; Check error code
cmp  rcx, 0x03
je   CheckWP

cmp  rcx, 0x01
jne  OrigRet

jmp  CheckFilters

; Check if WP violation
CheckWP:
push rcx
push rax
mov  rcx, cr2
shr  rcx, 9
mov  rax, 0x7FFFFFFFF8
and  rcx, rax
mov  rax, 0xDEADBEEFCAFEBABE
add  rax, rcx
mov  rax, [rax]
bt   rax, 1
pop  rax
pop  rcx
jnc  OrigRet

; Check filters
CheckFilters:
push rax
mov  rax, 0xDEADBEEFCAFEBABE
cmp  r8, rax
pop  rax
jb   SmapReturn

push rax
mov  rax, 0xDEADBEEFCAFEBABE
cmp  r8, rax
pop  rax
ja   SmapReturn

; Disable SMAP
stac

; Info offset calc prologue
nop
push rcx
push rdx
push r8
push r9

; Get current CPU
mov  rax, [gs:dword 0xEAEA]
and  rax, 0xFF
mov  r8, rax

; Get size of entry
mov  rax, 0xDEADBEEFCAFEBABE

; Get info entry offset
mul  r8
mov  r8, rax

; Info area address
mov  rax, 0xDEADBEEFCAFEBABE
add  rax, r8

; Info offset calc epilogue
pop  r9
pop  r8
pop  rdx
pop  rcx

; Store fault info
mov  [rax + 0xEAEA], rcx
mov  [rax + 0xEAEA], r8
mov  [rax + 0xEAEA], rdx
mov  [rax + 0xEAEA], r9
mov  r9, cr3
mov  [rax + 0xEAEA], r9
mov  rcx, rax
rdtsc
mov  [rcx + 0xEAEA], eax
mov  [rcx + 0xEAEA], edx

; Save XMM state
sub     rsp, 16
movdqu  dqword [rsp], xmm0
sub     rsp, 16
movdqu  dqword [rsp], xmm1
sub     rsp, 16
movdqu  dqword [rsp], xmm2
sub     rsp, 16
movdqu  dqword [rsp], xmm3
sub     rsp, 16
movdqu  dqword [rsp], xmm4
sub     rsp, 16
movdqu  dqword [rsp], xmm5

; Form a string
push rcx
push rdx
push r8
push r9
; Args
push rcx
; Output buffer
add  rcx, 0xADADADA
; Buf size
mov  rdx, 0xADADADA
; Buf size
mov  r8, 0xADADADA
; Format string address
mov  rax, 0xDEADBEEFCAFEBABE
mov  r9, rax
; Home space
sub  rsp, 0x20
; vsnprintf_s address
mov  rax, 0xDEADBEEFCAFEBABE
call rax
add  rsp, 0x28
pop  r9
pop  r8
pop  rdx
pop  rcx

; Output buffer offset
add  rcx, 0xADADADA

; Debug output
push rcx
push rdx
push r8
push r9
mov  r8, rcx
mov  rcx, 0x4E
mov  rdx, 0x00
; DbgPrintEx address
mov  rax, 0xDEADBEEFCAFEBABE
sub  rsp, 0x30
call rax
add  rsp, 0x30
pop  r9
pop  r8
pop  rdx
pop  rcx

; Restore XMM state
movdqu  xmm5, dqword [rsp]
add     rsp, 16
movdqu  xmm4, dqword [rsp]
add     rsp, 16
movdqu  xmm3, dqword [rsp]
add     rsp, 16
movdqu  xmm2, dqword [rsp]
add     rsp, 16
movdqu  xmm1, dqword [rsp]
add     rsp, 16
movdqu  xmm0, dqword [rsp]
add     rsp, 16

SmapReturn:

; Set TF, AC and ID
mov  rax, [rsp + 0x58]
or   rax, 0x240100
mov  [rsp + 0x58], rax

; Restore IRQL
pop  rax
mov  cr8, rax

pop  r11
pop  r10
pop  r9
pop  r8
pop  rdx
pop  rcx
pop  rax
add  rsp, 8
iretq

OrigRet:

; Restore IRQL
pop  rax
mov  cr8, rax

; Pounce back to the original handler
pop  r11
pop  r10
pop  r9
pop  r8
pop  rdx
pop  rcx
mov  rax, 0xDEADBEEFCAFEBABE
xchg [rsp], rax
ret
