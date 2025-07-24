INCLUDE Instruction.inc

.CODE

SimpleInstructionFunc1X86 PROC C

    xor     eax, eax
    mov     eax, PRESET_RETURN_VALUE
    ret

SimpleInstructionFunc1X86 ENDP

END