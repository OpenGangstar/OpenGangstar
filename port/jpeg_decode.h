#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OpenGangstarJpeg {
    uint32_t width, height;
    unsigned char* rgb; /* malloc-owned top-down RGB24, released by the function below. */
    int error_code; /* IJG message code; -1 denotes a native input/dimension/allocation bound. */
    char error[200];
    long warnings;
} OpenGangstarJpeg;

/* Entire setjmp/longjmp lifetime is inside C. No C++ automatic objects are
 * crossed. Input stays borrowed only for this call. Result must be fresh;
 * failure returns a zero image and a diagnostic, success returns owned RGB. */
int opengangstar_decode_jpeg(const unsigned char* bytes, size_t length, OpenGangstarJpeg* result);
void opengangstar_free_jpeg(OpenGangstarJpeg* result);

#ifdef __cplusplus
}
#endif
