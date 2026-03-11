#include "syscalls.h"
#include "types.h"
#include "video.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "../include/userland_api.h"

extern void irq_save(void);
extern void irq_restore(uint32_t flags);

void syscall_dispatch(struct syscall_regs *regs) {
    switch (regs->eax) {
        case SYSCALL_GFX_CLEAR:
            video_clear((uint8_t)(regs->ebx & 0xFFu));
            video_flip();
            regs->eax = 0;
            break;

        case SYSCALL_GFX_RECT:
            gfx_rect((int)regs->ebx, (int)regs->ecx, (int)regs->edx, (int)regs->esi,
                     (uint8_t)(regs->edi & 0xFFu));
            video_flip();
            regs->eax = 0;
            break;

        case SYSCALL_GFX_TEXT:
            gfx_text((int)regs->ebx, (int)regs->ecx, (const char *)(uintptr_t)regs->edx,
                     (uint8_t)(regs->esi & 0xFFu));
            video_flip();
            regs->eax = 0;
            break;

        case SYSCALL_INPUT_MOUSE: {
            struct mouse_state *out_state = (struct mouse_state *)(uintptr_t)regs->ebx;
            regs->eax = mouse_read(out_state) ? 1u : 0u;
            break;
        }

        case SYSCALL_INPUT_KEY:
            regs->eax = (uint32_t)keyboard_read();
            break;

        case SYSCALL_SLEEP:
            __asm__ volatile("hlt");
            regs->eax = 0;
            break;

        case SYSCALL_TIME_TICKS:
            regs->eax = timer_get_ticks();
            break;

        case SYSCALL_GFX_INFO: {
            struct video_mode *out = (struct video_mode *)(uintptr_t)regs->ebx;
            if (out != NULL) {
                struct video_mode *mode = video_get_mode();
                out->fb_addr = mode->fb_addr;
                out->width = mode->width;
                out->height = mode->height;
                out->pitch = mode->pitch;
                out->bpp = mode->bpp;
            }
            regs->eax = 0;
            break;
        }

        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
