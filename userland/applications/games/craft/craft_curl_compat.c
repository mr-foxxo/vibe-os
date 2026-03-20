#include <curl/curl.h>
#include <stdarg.h>

struct VibeCurl {
    const char *url;
    const char *postfields;
    void *writedata;
    void *writefunction;
    long response_code;
};

int curl_global_init(long flags) { (void)flags; return CURLE_OK; }
void curl_global_cleanup(void) {}
CURL *curl_easy_init(void) {
    static struct VibeCurl curl;
    curl.url = 0;
    curl.postfields = 0;
    curl.writedata = 0;
    curl.writefunction = 0;
    curl.response_code = 0;
    return &curl;
}
void curl_easy_cleanup(CURL *curl) { (void)curl; }
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...) {
    va_list ap;
    va_start(ap, option);
    if (curl) {
        switch (option) {
        case CURLOPT_URL:
            curl->url = va_arg(ap, const char *);
            break;
        case CURLOPT_POSTFIELDS:
            curl->postfields = va_arg(ap, const char *);
            break;
        case CURLOPT_WRITEDATA:
            curl->writedata = va_arg(ap, void *);
            break;
        case CURLOPT_WRITEFUNCTION:
            curl->writefunction = va_arg(ap, void *);
            break;
        default:
            (void)va_arg(ap, void *);
            break;
        }
    }
    va_end(ap);
    return CURLE_OK;
}
CURLcode curl_easy_perform(CURL *curl) {
    if (curl) {
        curl->response_code = 501;
    }
    return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...) {
    va_list ap;
    va_start(ap, info);
    if (curl && info == CURLINFO_RESPONSE_CODE) {
        long *out = va_arg(ap, long *);
        if (out) {
            *out = curl->response_code;
        }
    }
    va_end(ap);
    return CURLE_OK;
}
