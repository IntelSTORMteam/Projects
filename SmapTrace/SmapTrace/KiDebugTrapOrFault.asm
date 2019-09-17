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

; KiDebugTrapOrFault entry

; Debug or nop
nop

; Prologue
push rax

; Check if a SMAP event (ID flag)
mov  rax, [rsp + 0x18]
bt   rax, 21
jnc  OrigRet

; Set SMAP, reset TF
and  rax, 0x7FDBFEFF
mov  [rsp + 0x18], rax

; Epilogue
pop  rax

; Done
iretq

OrigRet:
mov  rax, 0xDEADBEEFCAFEBABE
xchg [rsp], rax
ret
