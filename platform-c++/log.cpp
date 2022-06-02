#include "log.h"

#include <mutex>
#include <vector>

#include "singleton.hpp"
#include "logger.h"
#include "abuff.h"
#include "ifos.h"

#ifdef _WIN32
#define snprintf(buffer, size, ...) \
    _snprintf_s(buffer, size, _TRUNCATE, __VA_ARGS__)
#endif

namespace nsp {
    namespace toolkit {
        namespace xlog {
            /////////////////////// loex ///////////////////////
            loex::loex(enum log_levels level) : level_(level)
            {
                ifos_path_buffer_t holder;
                if (NSP_SUCCESS(ifos_getpename(&holder))) {
                    strcpy(module_, holder.u.st);
                }
                str_[0] = 0;
            }

            loex::loex(const char *module, enum log_levels level) : level_(level)
            {
                if (module) {
                    crt_strcpy(module_, cchof(module_), module);
                }else{
                    ifos_path_buffer_t holder;
                    if (NSP_SUCCESS(ifos_getpename(&holder))) {
                        strcpy(module_, holder.u.st);
                    }
                }
                str_[0] = 0;
            }

            loex::~loex() {
                int target = kLogTarget_Filesystem | kLogTarget_Stdout;
                if (0 != str_[0]) { // 以此限制设置日志分片的对象，析构阶段不会真实调用日志输出
                    if (level_ & kLogLevel_Trace) {
                        target &= ~kLogTarget_Stdout;
                    }
                    ::log_save(module_, level_, target, "%s", str_);
                }
            }

            loex &loex::operator<<(const wchar_t *str) {
                if (str) {
#if _WIN32
                    crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%ws", str);
#endif
                }
                return *this;
            }

            loex &loex::operator<<(const char *str) {
                if (str) {
                    crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%s", str);
                }
                return *this;
            }

            loex &loex::operator<<(int32_t n) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%d", n);
                return *this;
            }

            loex &loex::operator<<(uint32_t n) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%u", n);
                return *this;
            }

            loex &loex::operator<<(int16_t n) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%d", n);
                return *this;
            }

            loex &loex::operator<<(uint16_t n) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%u", n);
                return *this;
            }

            loex &loex::operator<<(int64_t n) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), INT64_STRFMT, n);
                return *this;
            }

            loex &loex::operator<<(uint64_t n) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), UINT64_STRFMT, n);
                return *this;
            }

            loex &loex::operator<<(const std::basic_string<char> &str) {
                if (str.size() > 0) {
                    crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%s", str.c_str());
                }
                return *this;
            }

            loex &loex::operator<<(void *ptr) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%p", ptr);
                return *this;
            }

            loex &loex::operator<<(void **ptr) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%p", ptr);
                return *this;
            }

            loex &loex::operator<<(const hex &ob) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%08X", ob.__auto_t);
                return *this;
            }

            loex &loex::operator<<(float f) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%g", f);
                return *this;
            }

            loex &loex::operator<<(double lf) {
                crt_sprintf(&str_[strlen(str_)], sizeof ( str_) - strlen(str_), "%g", lf);
                return *this;
            }

            //			loex &loex::operator << ( const std::_Smanip<std::streamsize> &sp )
            //			{
            //				strsize_ = sp._Manarg;
            //				return *this;
            //			}

        } // xlog
    } // toolkit
} // namespace nsp
