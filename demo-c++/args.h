#if !defined ARGS_H
#define ARGS_H

#include "endpoint.h"

// type
#define SESS_TYPE_UNKNOWN       (-1)
#define SESS_TYPE_SERVER         (0)
#define SESS_TYPE_CLIENT        (1)

// mode
#define CS_MODE_ERROR			(0)
#define CS_MODE_ESCAPE_TASK     (1)     // ��ͨ���ݽ�������
#define CS_MODE_MUST_BE_CLIENT  (CS_MODE_ESCAPE_TASK)
#define CS_MODE_FILE_UPLOAD     (2)     // �ļ��ϴ�ģʽ
#define CS_MODE_FILEMODE		(CS_MODE_FILE_UPLOAD)
#define CS_MODE_FILE_DOWNLOAD   (3)     // �ļ�����ģʽ

extern void display_usage();
extern int check_args(int argc, char **argv);
extern int buildep(nsp::tcpip::endpoint &ep);

extern int gettype(); // ��ȡ�������
extern int getpkgsize(); // ��ȡ��֡��С
extern int getinterval(); // ��ȡ���
extern int getmode();  // ��ȡ������Ϊ��ʽ
extern const char *getfile(int *len); // ��ȡIO�ļ�
extern int getwinsize(); // ��ȡ���ڴ�С

#endif