#ifndef BASE_LOG_LOG_HPP
#define BASE_LOG_LOG_HPP

#include <cstdarg>
#include <string>
#include <iomanip>
#include <cstdint>

#include "icom/logger.h"

namespace nsp {
    namespace toolkit {
        namespace xlog {

            struct hex {
                hex(unsigned char c) : __cb(1) {
                    __byte8 = c;
                }

                hex(char c) : __cb(1) {
                    __character = c;
                }

                hex(uint32_t n) : __cb(4) {
                    __integer = n;
                }

                hex(int32_t n) : __cb(4) {
                    __integer = n;
                }

                hex(uint16_t n) : __cb(2) {
                    __byte16 = n;
                }

                hex(int16_t n) : __cb(2) {
                    __word = n;
                }

                hex(void *ptr) : __cb(4) {
                    __ptr = ptr;
                }

                union {
                    unsigned char __byte8;
                    char __character;
                    int32_t __integer;
                    uint16_t __byte16;
                    int16_t __word;
                    uint32_t __dword;
                    void *__ptr;
                    int __auto_t;
                };
                int __cb;
                hex(const hex &) = delete;
                hex(const hex &&) = delete;
                hex &operator=(const hex &) = delete;
            };

            class loex {
				char module_[LOG_MODULE_NAME_LEN];
                char str_[MAXIMUM_LOG_BUFFER_SIZE];
                enum log__levels level_;
                //std::streamsize strsize_;
                static void log_environment_init();
            public:
                loex( const char *module, enum log__levels level );
                loex(enum log__levels level);
                ~loex();
                loex(const loex &) = delete;
                loex(const loex &&) = delete;
                loex &operator=(const loex &) = delete;

                loex &operator<<(const char *str);
                loex &operator<<(const wchar_t *str);

                loex &operator<<(int32_t n);
                loex &operator<<(uint32_t n);

                loex &operator<<(int16_t n);
                loex &operator<<(uint16_t n);

                loex &operator<<(int64_t n);
                loex &operator<<(uint64_t n);

                loex &operator<<(const std::basic_string<char> &str);

                loex &operator<<(float f);
                loex &operator<<(double lf);

                loex &operator<<(void *ptr);
                loex &operator<<(void **ptr);

                loex &operator<<(const hex &ob);
            };
        } // xlog
    } // toolkit
} // nsp

#define nsptrace  nsp::toolkit::xlog::loex(kLogLevel_Trace)
#define nspinfo  nsp::toolkit::xlog::loex(kLogLevel_Info)
#define nspwarn  nsp::toolkit::xlog::loex(kLogLevel_Warning)
#define nsperror  nsp::toolkit::xlog::loex(kLogLevel_Error)

#define lotrace(name)  nsp::toolkit::xlog::loex(name, kLogLevel_Trace)
#define loinfo(name)  nsp::toolkit::xlog::loex(name, kLogLevel_Info)
#define lowarn(name)  nsp::toolkit::xlog::loex(name, kLogLevel_Warning)
#define loerror(name)  nsp::toolkit::xlog::loex(name, kLogLevel_Error)

#endif  // BASE_LOG_LOG_HPP
