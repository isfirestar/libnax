#if !defined ARGS_H
#define ARGS_H

#include "endpoint.h"

// type
#define SESS_TYPE_UNKNOWN       (-1)
#define SESS_TYPE_SERVER         (0)
#define SESS_TYPE_CLIENT        (1)

// mode
#define CS_MODE_ERROR			(0)
#define CS_MODE_ESCAPE_TASK     (1)     // 普通数据交互运行
#define CS_MODE_MUST_BE_CLIENT  (CS_MODE_ESCAPE_TASK)
#define CS_MODE_FILE_UPLOAD     (2)     // 文件上传模式
#define CS_MODE_FILEMODE		(CS_MODE_FILE_UPLOAD)
#define CS_MODE_FILE_DOWNLOAD   (3)     // 文件下载模式

extern void display_usage();
extern int check_args(int argc, char **argv);
extern int buildep(nsp::tcpip::endpoint &ep);

extern int gettype(); // 获取身份类型
extern int getpkgsize(); // 获取单帧大小
extern int getinterval(); // 获取间隔
extern int getmode();  // 获取运行行为方式
extern const char *getfile(int *len); // 获取IO文件
extern int getwinsize(); // 获取窗口大小

#endif