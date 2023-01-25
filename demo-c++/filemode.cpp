#include "filemode.h"
#include "os_util.hpp"
#include "icom/posix_ifos.h"
#include <errno.h>

#if _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

file_mode::file_mode(const std::string& path, uint32_t block_size) {
    file_path = path;
    this->block_size = block_size;
}

file_mode::~file_mode() {
    if (fd > 0) {
        posix__file_close(fd);
    }
}

int file_mode::open_it() {
    int retval;
    file_descriptor_t fd;

    retval = posix__file_open(file_path.c_str(), FF_RDACCESS | FF_OPEN_EXISTING, 0644, &fd);
    if (retval < 0) {
        return retval;
    }

    file_size = posix__file_fgetsize(fd);
    if (file_size < 0) {
        posix__file_close(fd);
        return -1;
    }
    return 0;
}

int file_mode::creat_it() {
    int retval;

    retval = posix__file_open(file_path.c_str(), FF_WRACCESS | FF_OPEN_EXISTING, 0644, &fd);
    if ( retval < 0) {
        retval = posix__file_open(file_path.c_str(), FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &fd);
    }
    return retval;
}

int file_mode::cover_it() {
    return posix__file_open(file_path.c_str(), FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &fd);
}

int file_mode::read_block(unsigned char *block) {
    // 因为下层执行同步读取， 因此不允许读取偏移超文件大小
    if (offset >= this->file_size) {
        return -1;
    }

    int rdcb;
    int rdcb_r;

    if (offset + block_size >= file_size) {
        rdcb_r = (int)(file_size - offset);
    } else {
        rdcb_r = block_size;
    }

    posix__file_seek(fd, offset);

    rdcb = posix__file_read(fd, block, rdcb_r);
    previous_offset = offset;
    offset += rdcb;
    return rdcb;
}

int file_mode::write_block(const unsigned char *block, uint64_t woff, int cb) {
    int wrcb = 0;

    posix__file_seek(fd, woff);

    wrcb = posix__file_write(fd, block, cb);
    offset += wrcb;
    return wrcb;
}

const std::string file_mode::getfilenam() const {
    auto of = file_path.find_last_of('/', 0);
    if (of == std::string::npos) {
        of = file_path.find_last_of('\\', 0);
    }
    if (of == std::string::npos) {
        return "";
    }
    return std::string(&file_path[of]);
}

const uint64_t file_mode::get_previous_offset() const {
    return previous_offset;
}

const uint32_t file_mode::get_blocksize() const {
    return this->block_size;
}
