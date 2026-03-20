#include <string.h>
#include <userland/applications/games/craft/upstream/src/auth.h>

int get_access_token(char *result, int length, char *username, char *identity_token) {
    (void)username;
    (void)identity_token;
    if (result && length > 0) {
        result[0] = '\0';
    }
    return 0;
}
