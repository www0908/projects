#ifndef BUFFER_h
#define BUFFER_h

#include<vector>
#include<string>
#include<cstring>
#include<atomic>
#include<cassert>
#include<sys/uio.h>
#include<unistd.h>
#include<stdexcept>
class Buffer{
public:
Buffer(int initBuffSize = 1024);
~Buffer() = default;
size_t WritableBytes () const;
size_t ReadableBytes () const;
size_t PrependableBytes () const;
const char * Peek() const;
char * BeginWrite();
const char * BeginWriteConst() const;
void Append(const char* str,size_t len);
void Append(const void* str,size_t len);
void Append(const Buffer& buff);
void Append(const std::string& str);
void EnsureWritable(size_t len);
void HasWritable(size_t len);
void MakeSpace_(size_t len);
void Retrieve(size_t len);
void RetrieveUntil(const char*end);
void RetrieveAll();
std::string RetrieveAllToStr();
std::string RetrieveToStr(size_t len);
ssize_t ReadFd(int fd,int * saveErrno);
ssize_t WriteFd(int fd,int * saveErrno);

private:
char * BeginPtr_();
const char * BeginPtr_() const;

std::vector<char> buffer_;
size_t readPos_;
size_t writePos_;


};
#endif