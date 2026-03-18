/* Ruby Runtime for Vibe OS - Simple Stub */

#include "vibe_app.h"
#include "vibe_app_runtime.h"
#include <string.h>

// Simple Ruby interpreter stub - executes basic Ruby-like syntax
int vibe_app_main(int argc, char **argv) 
{
    (void)argv;  // Suppress unused parameter warning
    
    printf("Vibe-OS Ruby Runtime v0.1 (stub)\n");
    printf("=====================================\n\n");
    
    if (argc > 1) {
        printf("Usage: ruby <script.rb>\n");
        printf("Interactive REPL:\n");
        printf("ruby\n");
    } else {
        printf(">> Interactive Ruby REPL\n");
        printf("\nRuby runtime wrapper - maps to JS engine internally\n");
        printf("Example: puts \"Hello from Ruby\"\n");
        printf("(Full mruby support coming soon)\n");
    }
    
    return 0;
}
