/*
 *	整合支持的一致性哈希算法到统一导出头
 *	anderson 2017-11-24
*/

#if !defined HASH_H
#define HASH_H

#include "compiler.h"
#include "abuff.h"

/*--------------------------------------------VFN1/VFN1a--------------------------------------------*/
PORTABLEAPI(uint32_t) vfn1_h32( const unsigned char *key, int length );
PORTABLEAPI(uint64_t) vfn1_h64( const unsigned char *key, int length );
PORTABLEAPI(uint32_t) vfn1a_h32( const unsigned char *key, int length );
PORTABLEAPI(uint64_t) vfn1a_h64( const unsigned char *key, int length );

PORTABLEAPI(uint32_t) crc32(uint32_t crc, const unsigned char *string, uint32_t size);

/*
 * base64_encode 例程对 @incb 长度的 @input 缓冲区作 BASE64 加密操作
 * base64__decode 例程对 @incb 长度的 @input 缓冲区作 BASE64 解密操作
 *
 * 参数:
 * @input 输入缓冲区
 * @incb 输入缓冲区字节长度
 * @utput 输出缓冲区
 * @outcb 输出缓冲区字节长度记录在 *outcb
 *
 * 备注:
 * 1. @input 必须是有效缓冲区地址
 * 2. @incb 必须保证大于等于0
 * 3. @output 如果为NULL, 则 @outcb 必须是有效缓冲区， 这种情况下 ， *outcb 将记录加密后的缓冲区长度， 但并不执行加密操作
 *
 * 返回:
 * 通用判定
 */
PORTABLEAPI(int) base64_encode_len(int binlength);
PORTABLEAPI(char *) base64_encode( const char *bindata, int binlength, char *base64);
PORTABLEAPI(int) base64_decode_len(const char * base64, int base64_len);
PORTABLEAPI(int) base64_decode( const char *base64, int base64_len, char *bindata);
/* for compatible reason, retain the older interface definition */
PORTABLEAPI(int) DEPRECATED("use base64 functions without \"2\" instead it") base64_encode2(const char *input, int incb, char *output, int *outcb);
PORTABLEAPI(int) DEPRECATED("use base64 functions without \"2\" instead it") base64_decode2(const char *input, int incb, char *output, int *outcb);

/* MD5 calc */
typedef struct __md5ctx *md5ctx_pt;
typedef abuff_type(16)  abuff_md5_result_t;
PORTABLEAPI(void) md5_init(md5ctx_pt md5ctx);
PORTABLEAPI(void) md5_update(md5ctx_pt md5ctx, const uint8_t *input, uint32_t inputLen);
PORTABLEAPI(void) md5_final(md5ctx_pt md5ctx, abuff_md5_result_t *digest);

/*
des_encrypt 过程， 使用DES对内存加密

参数:
 * @input 需要进行加密的缓冲区
 * @cb 加密缓冲区长度
 * @key 八个字节的密钥
 * @output  加密后的输出缓冲区, 缓冲区长度与 @input 一致

备注:
 * @key 可以为NULL, 如果@key为NULL, 则使用默认密钥
 * @cb 必须8字节对齐
 */
typedef abuff_type(8) abuff_des_key_t;
PORTABLEAPI(int) des_encrypt(const char* input,size_t cb,const abuff_des_key_t *key, char* output);
PORTABLEAPI(int) des_decrypt(const char* input,size_t cb,const abuff_des_key_t *key, char* output);

/**
 sha256 过程对 @orilen 长度的 @input 数据进行sha256加密， 加密结果通过 @out反馈给调用线程
 操作成功返回值等于 @out 的指针， 否则为NULL
 http://www.ttmd5.com/hash.php?type=9 这里可以在线验证
 **/
typedef abuff_type(32)  abuff_sha256_result_t;
PORTABLEAPI(unsigned char*) sha256(const unsigned char* input, int orilen, abuff_sha256_result_t *out);


/* The default hashing function uses SipHash implementation
 * in siphash.c. */
PORTABLEAPI(uint64_t) siphash(const uint8_t *in, const size_t inlen, const uint8_t *k);
PORTABLEAPI(uint64_t) siphash_nocase(const uint8_t *in, const size_t inlen, const uint8_t *k);

#endif
