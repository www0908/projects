#include <iostream>
#include <cassert>
#include <string>
#include "buffer.h"

void TestBasic() {
    Buffer buf(1024);
    assert(buf.ReadableBytes() == 0);
    assert(buf.WritableBytes() == 1024);
    assert(buf.PrependableBytes() == 0);

    // 写入数据
    std::string msg = "Hello WebServer!";
    buf.Append(msg);
    assert(buf.ReadableBytes() == msg.size());
    assert(buf.WritableBytes() == 1024 - msg.size());

    // 消费 5 字节 ("Hello")
    buf.Retrieve(5);
    assert(buf.ReadableBytes() == msg.size() - 5);
    assert(buf.PrependableBytes() == 5);

    // 消费全部剩余数据
    std::string rest = buf.RetrieveAllToStr();
    assert(rest == " WebServer!");
    assert(buf.ReadableBytes() == 0);
    assert(buf.PrependableBytes() == 0); // 全部消费完后自动重置回 0
    std::cout << "[PASS] TestBasic passed!" << std::endl;
}

void TestCompactionAndResize() {
    // 初始开辟仅 10 字节的小 Buffer，方便触发搬迁和扩容
    Buffer buf(10);

    buf.Append("12345", 5);  // 写入 5 字节，剩余 5 字节
    buf.Retrieve(3);          // 消费 3 字节（'1','2','3'），左边腾出 3 字节垃圾区

    assert(buf.ReadableBytes() == 2);
    assert(buf.PrependableBytes() == 3);
    assert(buf.WritableBytes() == 5);

    // 写入 7 字节：此时尾部只剩 5 字节，但垃圾区有 3 字节 (5 + 3 = 8 >= 7)
    // 这将触发内部搬迁平移（MakeSpace_ 的 else 分支），无需向系统申请内存扩容
    buf.Append("67890ab", 7);
    assert(buf.PrependableBytes() == 0); // 平移后读指针回位到 0
    assert(buf.ReadableBytes() == 9);

    // 写入超大文本，迫使触发真正的 resize 扩容
    std::string largeStr(50, 'x');
    buf.Append(largeStr);
    assert(buf.ReadableBytes() == 9 + 50);
    std::cout << "[PASS] TestCompactionAndResize passed!" << std::endl;
}

int main() {
    TestBasic();
    TestCompactionAndResize();
    std::cout << "All Buffer tests passed successfully!" << std::endl;
    return 0;
}