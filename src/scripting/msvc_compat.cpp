#ifdef __MINGW32__
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

// Compatibility stubs for linking MSVC-built libraries (like LibRaw) with MinGW.
// We DO NOT include stdio.h here to avoid "redefinition" or "previously defined" 
// errors with MinGW's inline functions. Instead, we provide external symbols 
// that redirect to the MinGW versions.

extern "C" {

// Forward declare types used in stdio
struct _iobuf;
typedef struct _iobuf FILE;

// Forward declare MinGW CRT functions
extern int vsnprintf(char* s, size_t n, const char* format, va_list arg);
extern int vfprintf(FILE* stream, const char* format, va_list arg);
extern int vsscanf(const char* s, const char* format, va_list arg);

// 1. Security check stubs (MSVC's /GS)
void __GSHandlerCheck() {}
void __GSHandlerCheck_EH4() {}
uintptr_t __security_cookie = 0xBBADF00D;
void __security_check_cookie(uintptr_t cookie) { (void)cookie; }

// 2. Standard I/O stubs
// These provide the external symbols that MSVC-built libraries expect.

int sprintf(char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int r = vsnprintf(str, 1048576, format, args); // 1MB buffer limit for safety
    va_end(args);
    return r;
}

int sscanf(const char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int r = vsscanf(str, format, args);
    va_end(args);
    return r;
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int r = vfprintf(stream, format, args);
    va_end(args);
    return r;
}

// 3. UCRT specific stubs (if LibRaw was built against UCRT)
// MSVC 2015+ uses these internal common functions.
int __stdio_common_vsprintf(unsigned __int64 options, char* str, size_t len, const char* format, void* locale, va_list valist) {
    (void)options; (void)locale;
    return vsnprintf(str, len, format, valist);
}

int __stdio_common_vsscanf(unsigned __int64 options, const char* input, size_t length, const char* format, void* locale, va_list valist) {
    (void)options; (void)length; (void)locale;
    return vsscanf(input, format, valist);
}

int __stdio_common_vfprintf(unsigned __int64 options, FILE* stream, const char* format, void* locale, va_list valist) {
    (void)options; (void)locale;
    return vfprintf(stream, format, valist);
}

// 4. Misc MSVC CRT stubs
void _invoke_watson(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved) {
    (void)expression; (void)function; (void)file; (void)line; (void)reserved;
}

} // extern "C"
#endif
