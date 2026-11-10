                PRESERVE8
                THUMB
                AREA RESET, DATA, READONLY
                EXPORT __Vectors
__Vectors       DCD 0x20003c00
                DCD Reset_Handler
                DCD Default_Handler
                DCD Default_Handler
                DCD 0,0,0,0,0,0,0
                DCD Default_Handler
                DCD 0,0
                DCD Default_Handler
                DCD SysTick_Handler
                DCD Keyboard_Handler
                DCD UART_Handler
                AREA |.text|, CODE, READONLY
                EXPORT Reset_Handler
                EXPORT __main
                IMPORT main
                IMPORT tick_isr
                IMPORT key_isr
                IMPORT uart_isr
__main
Reset_Handler   BL main
                B Default_Handler
; Hardware stacks caller-saved registers; push two words to retain 8-byte alignment.
SysTick_Handler PUSH {R4,LR}
                BL tick_isr
                POP {R4,PC}
Keyboard_Handler PUSH {R4,LR}
                BL key_isr
                POP {R4,PC}
UART_Handler    PUSH {R4,LR}
                BL uart_isr
                POP {R4,PC}
Default_Handler B Default_Handler
                END
