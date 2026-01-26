// SPDX-License-Identifier: GPL-3.0-or-later
#include <drivers/vga.h>

// VGA内存地址
static u16* const VGA_MEMORY = (u16*)0xB8000;

// 当前光标位置
static u32 cursor_x = 0;
static u32 cursor_y = 0;

// 当前颜色
static u8 current_color = 0x0F;  // 白字黑底

// 初始化VGA
void vga_init(void) {
    cursor_x = 0;
    cursor_y = 0;
    current_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
}

// 清屏
void vga_clear(void) {
    for (u32 y = 0; y < VGA_HEIGHT; y++) {
        for (u32 x = 0; x < VGA_WIDTH; x++) {
            const u32 index = y * VGA_WIDTH + x;
            VGA_MEMORY[index] = vga_entry(' ', current_color);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

// 设置颜色
void vga_set_color(enum vga_color fg, enum vga_color bg) {
    current_color = vga_entry_color(fg, bg);
}

// 输出字符
void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        }
        const u32 index = cursor_y * VGA_WIDTH + cursor_x;
        VGA_MEMORY[index] = vga_entry(' ', current_color);
    } else {
        const u32 index = cursor_y * VGA_WIDTH + cursor_x;
        VGA_MEMORY[index] = vga_entry(c, current_color);
        cursor_x++;
    }
    
    // 检查是否需要滚动
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll();
        cursor_y = VGA_HEIGHT - 1;
    }
    
    // 更新硬件光标
    vga_update_cursor();
}

// 输出字符串
void vga_puts(const char* str) {
    while (*str) {
        vga_putc(*str++);
    }
}

// 滚动屏幕
void vga_scroll(void) {
    // 将第2行到最后一行上移一行
    for (u32 y = 1; y < VGA_HEIGHT; y++) {
        for (u32 x = 0; x < VGA_WIDTH; x++) {
            const u32 src_index = y * VGA_WIDTH + x;
            const u32 dst_index = (y - 1) * VGA_WIDTH + x;
            VGA_MEMORY[dst_index] = VGA_MEMORY[src_index];
        }
    }
    
    // 清空最后一行
    for (u32 x = 0; x < VGA_WIDTH; x++) {
        const u32 index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        VGA_MEMORY[index] = vga_entry(' ', current_color);
    }
}

// 更新硬件光标
void vga_update_cursor(void) {
    const u16 position = cursor_y * VGA_WIDTH + cursor_x;
    
    // 发送位置低字节
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(position & 0xFF));
    
    // 发送位置高字节
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((position >> 8) & 0xFF));
}

// 格式化输出（简化版）
void vga_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vga_vprintf(fmt, args);
    va_end(args);
}

// 可变参数格式化输出
void vga_vprintf(const char* fmt, va_list args) {
    char buffer[32];
    const char* p = fmt;
    
    while (*p) {
        if (*p != '%') {
            vga_putc(*p++);
            continue;
        }
        
        p++;  // 跳过'%'
        
        switch (*p) {
            case 'd':
            case 'i': {
                int num = va_arg(args, int);
                itoa(num, buffer, 10);
                vga_puts(buffer);
                break;
            }
            case 'u': {
                unsigned int num = va_arg(args, unsigned int);
                utoa(num, buffer, 10);
                vga_puts(buffer);
                break;
            }
            case 'x':
            case 'X': {
                unsigned int num = va_arg(args, unsigned int);
                utoa(num, buffer, 16);
                vga_puts(buffer);
                break;
            }
            case 's': {
                char* str = va_arg(args, char*);
                if (str) vga_puts(str);
                else vga_puts("(null)");
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                vga_putc(c);
                break;
            }
            case '%': {
                vga_putc('%');
                break;
            }
        }
        p++;
    }
}