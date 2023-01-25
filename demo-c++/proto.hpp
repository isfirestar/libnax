#if !defined ESCAPE_PROTO_H
#define ESCAPE_PROTO_H

#include <stdint.h>

#include "serialize.hpp"

struct proto_head : public nsp::proto::proto_interface {
    virtual const int length() const override {
        return id.length() + type.length() + ver.length() + err.length() + tx_timestamp.length();
    }

    virtual unsigned char *serialize(unsigned char *bytes) const override {
        unsigned char *pos = bytes;
        pos = id.serialize(pos);
        pos = type.serialize(pos);
        pos = ver.serialize(pos);
        pos = err.serialize(pos);
        pos = tx_timestamp.serialize(pos);
        return pos;
    }

    virtual const unsigned char *build(const unsigned char *bytes, int &cb) override {
		const unsigned char *pos = bytes;
        pos = id.build(pos, cb);
        pos = type.build(pos, cb);
        pos = ver.build(pos, cb);
        pos = err.build(pos, cb);
        pos = tx_timestamp.build(pos, cb);
        return pos;
    }

    nsp::proto::proto_crt_t<uint32_t> id;
    nsp::proto::proto_crt_t<uint32_t> type;
    nsp::proto::proto_crt_t<uint32_t> ver;
    nsp::proto::proto_crt_t<int> err;
    nsp::proto::proto_crt_t<uint64_t> tx_timestamp;
};

#define PKTTYPE_LOGIN           (0x00001001)
#define PKTTYPE_LOGIN_ACK       (0x80001001)

struct proto_login : public nsp::proto::proto_interface {
	proto_login() {
		head.type = PKTTYPE_LOGIN;
	}
    virtual const int length() const override {
        return head.length() + mode.length();
    }

    virtual unsigned char *serialize(unsigned char *bytes) const override {
        unsigned char *pos = bytes;
        pos = head.serialize(pos);
        pos = mode.serialize(pos);
        return pos;
    }

    virtual const unsigned char *build(const unsigned char *bytes, int &cb) override {
		const unsigned char *pos = bytes;
        pos = head.build(pos, cb);
        pos = mode.build(pos, cb);
        return pos;
    }

    struct proto_head head;
    nsp::proto::proto_crt_t<uint32_t> mode;
};

struct proto_login_ack : public proto_head {
	proto_login_ack() {
		this->type = PKTTYPE_LOGIN_ACK;
	}
};

#define PKTTYPE_ENABLE_FILEMODE         (0x00001002)
#define PKTTYPE_ENABLE_FILEMODE_ACK     (0x80001002)

#define ENABLE_FILEMODE_TEST            (1)     // 尝试建立文件模式， 如果目标文件存在， 则 head::err 反馈为 EEXIST
#define ENABLE_FILEMODE_FORCE           (2)     // 如果目标文件存在， 则强制覆盖

struct proto_enable_filemode : public nsp::proto::proto_interface  {
	proto_enable_filemode() {
		head.type = PKTTYPE_ENABLE_FILEMODE;
	}

    virtual const int length() const override {
        return head.length() + path.length() + block_size.length() + mode.length();
    }

    virtual unsigned char *serialize(unsigned char *bytes) const override {
        unsigned char *pos = bytes;
        pos = head.serialize(pos);
        pos = path.serialize(pos);
        pos = block_size.serialize(pos);
		pos = mode.serialize( pos );
        return pos;
    }

    virtual const unsigned char *build(const unsigned char *bytes, int &cb) override {
		const unsigned char *pos = bytes;
        pos = head.build(pos, cb);
        pos = path.build(pos, cb);
        pos = block_size.build(pos, cb);
		pos = mode.build( pos, cb );
        return pos;
    }

    struct proto_head head;
    nsp::proto::proto_string_t<char> path;
    nsp::proto::proto_crt_t<uint32_t> block_size;
    nsp::proto::proto_crt_t<uint32_t> mode;
};
struct proto_enable_filemode_ack : public proto_head {
	proto_enable_filemode_ack() {
		this->type = PKTTYPE_ENABLE_FILEMODE_ACK;
	}
};

#define PKTTYPE_FILE_BLOCK          (0x00001003)
#define PKTTYPE_FILE_BLOCK_ACK      (0x80001003)

#define OFFSET_EOF                ((uint64_t)(~0))

struct proto_file_block : public nsp::proto::proto_interface  {
	proto_file_block() {
		head.type = PKTTYPE_FILE_BLOCK;
	}
    virtual const int length() const override {
        return head.length() + offset.length() + data.length();
    }

    virtual unsigned char *serialize(unsigned char *bytes) const override {
        unsigned char *pos = bytes;
        pos = head.serialize(pos);
        pos = offset.serialize(pos);
        pos = data.serialize(pos);
        return pos;
    }

    virtual const unsigned char *build(const unsigned char *bytes, int &cb) override {
		const unsigned char *pos = bytes;
        pos = head.build(pos, cb);
        pos = offset.build(pos, cb);
        pos = data.build(pos, cb);
        return pos;
    }

    struct proto_head head;
    nsp::proto::proto_crt_t<uint64_t> offset; // @u 填写本次传送数据偏移， 直到 OFFSET_EOF, @d 忽略
    nsp::proto::proto_string_t<unsigned char> data; // @u 本次传输数据块， @d 忽略
};

struct proto_file_block_ack : public nsp::proto::proto_interface {
	proto_file_block_ack() {
		head.type = PKTTYPE_FILE_BLOCK_ACK;
	}

    virtual const int length() const override {
        return head.length() + offset.length() + data.length();
    }

    virtual unsigned char *serialize(unsigned char *bytes) const override {
        unsigned char *pos = bytes;
        pos = head.serialize(pos);
        pos = offset.serialize(pos);
        pos = data.serialize(pos);
        return pos;
    }

    virtual const unsigned char *build(const unsigned char *bytes, int &cb) override {
		const unsigned char *pos = bytes;
        pos = head.build(pos, cb);
        pos = offset.build(pos, cb);
        pos = data.build(pos, cb);
        return pos;
    }

    struct proto_head head;
    nsp::proto::proto_crt_t<uint64_t> offset; // @u 忽略， @d 填写本次传送数据偏移，直到 OFFSET_EOF
    nsp::proto::proto_vector_t<nsp::proto::proto_crt_t<uint8_t>> data; // @u 忽略， @d 本次传输数据块
};

#define PKTTYPE_ESCAPE_TASK             (0x00001004)
#define PKTTYPE_ESCAPE_TASK_ACK         (0x80001004)

struct proto_escape_task : public nsp::proto::proto_interface {
	proto_escape_task() {
		head.type = PKTTYPE_ESCAPE_TASK;
	}

    virtual ~proto_escape_task() {
        ;
    }

    virtual const int length() const override {
        return head.length() + contex.length();
    }

    virtual unsigned char *serialize(unsigned char *bytes) const override {
        unsigned char *pos = bytes;
        pos = head.serialize(pos);
        pos = contex.serialize(pos);
        return pos;
    }

    virtual const unsigned char *build(const unsigned char *bytes, int &cb) override {
        const unsigned char *pos = bytes;
        pos = head.build(pos, cb);
        pos = contex.build(pos, cb);
        return pos;
    }

    struct proto_head head;
    nsp::proto::proto_string_t<char>  contex;
};

#endif // !ESCAPE_PROTO_H
