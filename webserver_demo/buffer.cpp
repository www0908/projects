#include "buffer.h"
#include <algorithm>
#include <stdexcept>
#include <cassert>
#include <sys/uio.h>
#include <unistd.h>

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

size_t Buffer::PrependableBytes() const {
    return readPos_;
}

const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

// 修复：一个 const，一个非 const，规避重复定义
const char* Buffer::BeginPtr_() const {
    return buffer_.data();
}

char* Buffer::BeginPtr_() {
    return buffer_.data();
}

void Buffer::HasWritable(size_t len) {
    writePos_ += len;
}

void Buffer::EnsureWritable(size_t len) {
    if (WritableBytes() < len) {
        MakeSpace_(len);
    }
}

// 补充缺失的 Retrieve：负责游标的安全推进
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    if (len < ReadableBytes()) {
        readPos_ += len;
    } else {
        RetrieveAll();
    }
}

void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end);
    assert(end <= BeginWriteConst());
    Retrieve(end - Peek());
}

void Buffer::RetrieveAll() {
    // 斩杀 bzero 性能黑洞，直接把游标归零，新数据自然会覆盖旧内存
    readPos_ = 0;
    writePos_ = 0;
}

std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

std::string Buffer::RetrieveToStr(size_t len) {
    if (len > ReadableBytes()) {
        len = ReadableBytes();
    }
    std::string str(Peek(), len);
    Retrieve(len); // 用 Retrieve 更直接安全
    return str;
}

// 核心管家：碎片整理与几何扩容
void Buffer::MakeSpace_(size_t len) {
    if (WritableBytes() + PrependableBytes() < len) {
        // 存量空间彻底不够，触发几何倍增扩容，规避频繁 new/delete
        size_t new_size = std::max(writePos_ + len, buffer_.size() * 2);
        buffer_.resize(new_size);
    } else {
        // 碎片整理：前段废弃空间+尾部剩余空间足够，将有效数据平移到 vector 头部榨干空间
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}

// 主 Append 挂满装甲
void Buffer::Append(const char* str, size_t len) {
    if (len == 0) return; 
    
    // 杜绝 Release 模式 assert 失效
    if (str == nullptr) {
        throw std::invalid_argument("Append str is nullptr");
    }

    // 严防无符号整数溢出：防御黑客超大报文攻击
    if (writePos_ + len < writePos_) {
        throw std::overflow_error("Buffer length overflow");
    }

    // 封死自拷贝引发的悬空指针灾难
    if (str >= buffer_.data() && str < buffer_.data() + buffer_.size()) {
        std::string temp(str, len); 
        MakeSpace_(len);            
        std::copy(temp.data(), temp.data() + len, BeginWrite());
    } else {
        EnsureWritable(len);
        std::copy(str, str + len, BeginWrite());
    }
    HasWritable(len);
}

void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

void Buffer::Append(const void* data, size_t len) {
    if (data == nullptr) return; // 拦截 static_cast 转换空指针
    Append(static_cast<const char*>(data), len);
}

void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

// 彻底剥离系统调用错误处理：保留 errno 原样透传
ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char extrabuff[65535];
    struct iovec iov[2];
    const size_t writeable = WritableBytes();
    
    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writeable;
    iov[1].iov_base = extrabuff;
    iov[1].iov_len = sizeof(extrabuff);
    
    const ssize_t len = readv(fd, iov, 2);
    
    if (len < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(len) <= writeable) {
        writePos_ += len;
    } else {
        writePos_ = buffer_.size();
        Append(extrabuff, len - writeable);
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize); 
    
    if (len < 0) {
        *saveErrno = errno;
    }else{
    readPos_ += len;}
    return len;
}