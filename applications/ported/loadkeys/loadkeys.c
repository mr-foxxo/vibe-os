#include "compat/include/compat.h"

void print_help() {
    char available_layouts[256];
    char current_layout[32];

    if (vibe_app_keyboard_get_available_layouts(available_layouts, sizeof(available_layouts)) != 0) {
        strcpy(available_layouts, "(indisponivel)");
    }
    if (vibe_app_keyboard_get_layout(current_layout, sizeof(current_layout)) != 0) {
        strcpy(current_layout, "(indisponivel)");
    }

    printf("Uso: loadkeys <layout>\n");
    printf("\n");
    printf("Layouts disponíveis: %s\n", available_layouts);
    printf("Layout atual: %s\n", current_layout);
}

int vibe_app_main(int argc, char* argv[]) {
    if (argc != 2) {
        print_help();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-help") == 0) {
        print_help();
        return 0;
    }

    if (vibe_app_keyboard_set_layout(argv[1]) != 0) {
        printf("layout desconhecido: %s\n", argv[1]);
        return 1;
    }

    printf("Layout do teclado alterado para: %s\n", argv[1]);

    return 0;
}
