#include <stage2/include/graphics.h>
#include <stage2/include/video.h>

static char uppercase_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static uint8_t font_row_bits(char c, int row) {
    c = uppercase_char(c);

    switch (c) {
        case 'A': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
            return g[row];
        }
        case 'B': {
            static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
            return g[row];
        }
        case 'C': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
            return g[row];
        }
        case 'D': {
            static const uint8_t g[7] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
            return g[row];
        }
        case 'E': {
            static const uint8_t g[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
            return g[row];
        }
        case 'F': {
            static const uint8_t g[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
            return g[row];
        }
        case 'G': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
            return g[row];
        }
        case 'H': {
            static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
            return g[row];
        }
        case 'I': {
            static const uint8_t g[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
            return g[row];
        }
        case 'J': {
            static const uint8_t g[7] = {0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
            return g[row];
        }
        case 'K': {
            static const uint8_t g[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
            return g[row];
        }
        case 'L': {
            static const uint8_t g[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
            return g[row];
        }
        case 'M': {
            static const uint8_t g[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
            return g[row];
        }
        case 'N': {
            static const uint8_t g[7] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11};
            return g[row];
        }
        case 'O': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
            return g[row];
        }
        case 'P': {
            static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
            return g[row];
        }
        case 'Q': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
            return g[row];
        }
        case 'R': {
            static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
            return g[row];
        }
        case 'S': {
            static const uint8_t g[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
            return g[row];
        }
        case 'T': {
            static const uint8_t g[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
            return g[row];
        }
        case 'U': {
            static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
            return g[row];
        }
        case 'V': {
            static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
            return g[row];
        }
        case 'W': {
            static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
            return g[row];
        }
        case 'X': {
            static const uint8_t g[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
            return g[row];
        }
        case 'Y': {
            static const uint8_t g[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
            return g[row];
        }
        case 'Z': {
            static const uint8_t g[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
            return g[row];
        }
        case '0': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
            return g[row];
        }
        case '1': {
            static const uint8_t g[7] = {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F};
            return g[row];
        }
        case '2': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
            return g[row];
        }
        case '3': {
            static const uint8_t g[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
            return g[row];
        }
        case '4': {
            static const uint8_t g[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
            return g[row];
        }
        case '5': {
            static const uint8_t g[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
            return g[row];
        }
        case '6': {
            static const uint8_t g[7] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
            return g[row];
        }
        case '7': {
            static const uint8_t g[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
            return g[row];
        }
        case '8': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
            return g[row];
        }
        case '9': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
            return g[row];
        }
        case '>': {
            static const uint8_t g[7] = {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10};
            return g[row];
        }
        case '<': {
            static const uint8_t g[7] = {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01};
            return g[row];
        }
        case ':': {
            static const uint8_t g[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
            return g[row];
        }
        case '-': {
            static const uint8_t g[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
            return g[row];
        }
        case '_': {
            static const uint8_t g[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
            return g[row];
        }
        case '.': {
            static const uint8_t g[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
            return g[row];
        }
        case '/': {
            static const uint8_t g[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
            return g[row];
        }
        case '\\': {
            static const uint8_t g[7] = {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00};
            return g[row];
        }
        case '[': {
            static const uint8_t g[7] = {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E};
            return g[row];
        }
        case ']': {
            static const uint8_t g[7] = {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E};
            return g[row];
        }
        case '=': {
            static const uint8_t g[7] = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00};
            return g[row];
        }
        case '+': {
            static const uint8_t g[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
            return g[row];
        }
        case '(': {
            static const uint8_t g[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
            return g[row];
        }
        case ')': {
            static const uint8_t g[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
            return g[row];
        }
        case '?': {
            static const uint8_t g[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
            return g[row];
        }
        case '!': {
            static const uint8_t g[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
            return g[row];
        }
        case ' ': return 0x00;
        default: {
            static const uint8_t g[7] = {0x1F, 0x01, 0x05, 0x09, 0x11, 0x00, 0x11};
            return g[row];
        }
    }
}

void gfx_char(int x, int y, char c, uint8_t color) {
    struct video_mode *mode = video_get_mode();
    uint8_t *bb = video_get_backbuffer();
    
    if (bb == NULL) return;
    
    for (int row = 0; row < 7; ++row) {
        const uint8_t bits = font_row_bits(c, row);
        for (int col = 0; col < 5; ++col) {
            if ((bits & (1u << (4 - col))) == 0u) {
                continue;
            }
            const int px = x + col;
            const int py = y + row;
            if (px < 0 || py < 0 || px >= (int)mode->width || py >= (int)mode->height) {
                continue;
            }
            bb[(py * mode->pitch) + px] = color;
        }
    }
}

void gfx_text(int x, int y, const char *text, uint8_t color) {
    int cx = x;
    int cy = y;

    while (*text != '\0') {
        const char c = *text++;
        if (c == '\n') {
            cx = x;
            cy += 8;
            continue;
        }

        gfx_char(cx, cy, c, color);
        cx += 6;
    }
}

void gfx_rect(int x, int y, int w, int h, uint8_t color) {
    struct video_mode *mode = video_get_mode();
    uint8_t *bb = video_get_backbuffer();
    
    if (bb == NULL || w <= 0 || h <= 0) {
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)mode->width) x1 = (int)mode->width;
    if (y1 > (int)mode->height) y1 = (int)mode->height;

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            bb[(py * mode->pitch) + px] = color;
        }
    }
}
