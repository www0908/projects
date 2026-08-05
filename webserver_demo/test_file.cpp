#include <iostream>
#include <fstream>
#include <string>

// 文件读取核心函数
std::string read_file_content(const std::string& filepath) {
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        std::cerr << "[-] 报错：找不到文件或没有读取权限 -> " << filepath << std::endl;
        return ""; 
    }
    
    std::string content;
    std::string line;
    
    while (std::getline(file, line)) {
        content += line + "\n";
    }
    
    file.close();
    return content;
}

int main() {
    std::cout << "=== 开始测试文件读取功能 ===" << std::endl;
    
    // 尝试读取当前目录下的 index.html
    std::string target_file = "index.html";
    std::string html_data = read_file_content(target_file);
    
    if (html_data.empty()) {
        std::cout << "读取失败或文件为空！请检查文件是否存在。" << std::endl;
    } else {
        std::cout << "[+] 读取成功！文件内容如下：\n";
        std::cout << "--------------------------------\n";
        std::cout << html_data;
        std::cout << "--------------------------------\n";
        // 验证一下字符串的长度
        std::cout << "总共读取了 " << html_data.length() << " 个字节。" << std::endl;
    }
    
    std::cout << "=== 测试结束 ===" << std::endl;
    return 0;
}
