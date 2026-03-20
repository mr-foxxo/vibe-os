#ifndef VIBEOS_CURL_H
#define VIBEOS_CURL_H

#include <stddef.h>

typedef struct VibeCurl CURL;
typedef int CURLcode;
typedef int CURLoption;
typedef int CURLINFO;

#define CURLE_OK 0
#define CURL_GLOBAL_DEFAULT 0
#define CURLOPT_URL 10002
#define CURLOPT_POSTFIELDS 10015
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_WRITEDATA 10001
#define CURLOPT_SSL_VERIFYPEER 64
#define CURLINFO_RESPONSE_CODE 0x200002

int curl_global_init(long flags);
void curl_global_cleanup(void);
CURL *curl_easy_init(void);
void curl_easy_cleanup(CURL *curl);
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL *curl);
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...);

#endif
