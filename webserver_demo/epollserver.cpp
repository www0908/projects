#include<iostream>
#include<cstring>
#include<unistd.h> 
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<sys/epoll.h>
#include<string>
#include<fcntl.h>
#include<sstream>
#include<unordered_map>
#include<fstream>
#include<cerrno>
#include "Threadpool.h"
#define PORT 8080
const int MAX_EVENTS = 1024; // 最大事件数
const int BUFFER_SIZE = 1024;//定义缓冲区大小
std::string parse_http_request(const char* buffer,ssize_t bytes_recevied){
    std::string raw_request(buffer, bytes_recevied);// 将接收到的缓冲区数据转换为字符串
    size_t line_end = raw_request.find("\r\n"); // 查找请求行结束
    if(line_end == std::string::npos){
        line_end = raw_request.find('\n');
        if(line_end == std::string::npos){
            std::cerr<<"无效的HTTP请求\n"<<std::endl;
            return "";
        }
    }
    std::string request_line =raw_request.substr(0,line_end);// 提取请求行
    std::istringstream request_stream(request_line);// 使用字符串流解析 请求行
    std::string method, path, version;// 定义变量存储请求方法、路径
    if(request_stream>>method>>path>>version){
        std::cout<<"请求方法: "<<method<<std::endl;
        std::cout<<"请求路径: "<<path<<std::endl;
        return path; // 返回请求路径
    }
    std::cerr<<"无法解析HTTP请求行\n"<<std::endl;
    return "";



}
// void parse_http_request(const char* buffer){
//     std::string raw_request(buffer);
//     size_t line_end = raw_request.find("\r\n"); // 查找请求行结束符
//     if(line_end == std::string::npos){// 检查是否找到行结束符
//         std::cerr<<"无效的HTTP请求\n"<<std::endl;
//         return;
//     }
//     std::string request_line =raw_request.substr(0,line_end);// 提取请求行
//     std::istringstream request_stream(request_line);// 使用字符串流解析请求行
//     std::string method, path, version;// 定义变量存储请求方法、路径和HTTP版本
//     if(request_stream>>method>>path>>version){
//         std::cout<<"请求方法: "<<method<<std::endl;
//         std::cout<<"请求路径: "<<path<<std::endl;
//         std::cout<<"HTTP版本: "<<version<<std::endl;
//         std::cout<<"==============================="<<std::endl;
//     }
// }
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);// 获取文件描述符的当前标志
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);// 设置非阻塞模式
}
std::string read_file_content (const std::string&filepath){
std::ifstream file(filepath);
if(!file.is_open()){
    std::cerr<<"无法打开文件"<<filepath<<std::endl;
    return"";

}
std::string content;
std::string line;
while(std::getline(file,line)){
    content +=line +"\n";

}
file.close();
return content;
};
int main(){
    ThreadPool pool(4);
    std::unordered_map<std::string, std::string> router = {
    // 注意加入 <head><meta charset="utf-8"></head>
    {"/","index.html"},
    {"/study/linux/tcp","tcp.html"},
    {"/user","user.html"}
};
     int epoll_fd = epoll_create1(0);// 创建epoll实例
    int server_fd =socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0){
        std::cerr<<"Socket 创建失败/n"<<std::endl;
        return -1;
    }
    int opt=1;//设置套接字选项
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));// 设置套接字选项，允许地址重用
    sockaddr_in address{};//定义服务器地址结构体
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;// 绑定到所有可用的网络接口
    address.sin_port = htons(PORT);// 将端口号转换为网络字节序
    if(bind(server_fd,(struct sockaddr*)&address,sizeof(address))<0){
        std::cerr<<"绑定失败\n"<<std::endl;
        return -1;
    }
    if(listen(server_fd,5)<0){
        std::cerr<<"监听失败\n"<<std::endl;
        return -1;
    }
    set_nonblocking(server_fd);// 设置服务器套接字为非阻塞模式
    struct epoll_event ev{};
    ev.events = EPOLLIN; // 设置事件类型为可读事件
    ev.data.fd = server_fd; // 将服务器套接字的文件描述符存
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev); // 将服务器套接字添加到epoll实例中
    struct epoll_event events[MAX_EVENTS]; // 用于存储就绪事件的数组
    std::cout << "epoll服务器已启动，监听端口 " << PORT << std::endl;
 while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(nfds == -1){
            std::cerr << "epoll_wait 错误\n" << std::endl;
            break;
        }

        for(int i = 0; i < nfds; i++){
            int fd = events[i].data.fd;

            if(fd == server_fd) {
                // 【前台接待】：主线程只负责接客
                sockaddr_in client_addr{};
                socklen_t client_addr_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
                
                if(client_fd < 0){
                    std::cerr << "accept 错误\n" << std::endl;
                    continue;
                }
                
                set_nonblocking(client_fd);
                // 加上了 EPOLLONESHOT，防止多个厨师抢同一个客人
                struct epoll_event ev{};
                ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT; 
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                std::cout << "新客户端连接，IP: " << client_ip << " 端口: " << ntohs(client_addr.sin_port) << std::endl;
           
            } else if(events[i].events & EPOLLIN) {
                // 【后厨干活】：把老客人的活儿打包扔给线程池！
                int client_fd = events[i].data.fd;
                
                // 【坑1】必须要把 &router 传进来，不然线程池里用不了路由表！
                pool.enqueue([client_fd, epoll_fd, &router]() {
                    char buffer[2048];
                    memset(buffer, 0, sizeof(buffer));
                    
                    // 【坑2】这里叫 bytes_read
                    int bytes_read = read(client_fd, buffer, sizeof(buffer));
                    
                    if(bytes_read > 0) {
                        std::cout << "[线程 " << std::this_thread::get_id() << "] 接客成功，收到请求数据：\n" << buffer << std::endl;

                        // --- 下面这些活儿，必须全都在 Lambda 里面干！ ---
                        std::string path = parse_http_request(buffer, bytes_read); 
                        std::string status_line;
                        std::string html_body;

                        if(path.empty()){
                            status_line = "HTTP/1.1 400 Bad Request\r\n";
                            html_body = "<html><body><h1>400 Bad Request</h1><p>请求不合法</p></body></html>";
                        } else {
                            auto it = router.find(path);
                            if(it != router.end()){
                                std::string filepath = it->second;
                                html_body = read_file_content(filepath);
                                if (html_body.empty()){
                                    status_line = "HTTP/1.1 500 Internal Server Error\r\n";
                                    html_body = "<html><head><meta charset=\"utf-8\"></head><body><h1>500 内部服务器错误</h1><p>文件丢失!</p></body></html>";
                                } else {
                                    status_line = "HTTP/1.1 200 OK\r\n";
                                }
                            } else {
                                status_line = "HTTP/1.1 404 Not Found\r\n";
                                html_body = "<html><head><meta charset=\"utf-8\"></head><body><h1>404 Not Found </h1><p>页面去火星了</p></body></html>";
                            }
                        }

                        // 拼装响应报文
                        std::string http_response = 
                            status_line + 
                            "Content-Type: text/html; charset=utf-8\r\n" 
                            "Content-Length: " + std::to_string(html_body.length()) + "\r\n"
                            "Connection: close\r\n" // 【坑3】因为加了 EPOLLONESHOT，我们先用短连接（发完就关）最稳妥
                            "\r\n" + 
                            html_body;
                        
                        // 【坑4】这里是 client_fd，不能用 fd！
                        send(client_fd, http_response.c_str(), http_response.length(), 0);
                        
                        // 发完网页，直接关掉连接，厨师下班！
                        close(client_fd); 

                    } else if(bytes_read == 0) {
                        std::cout << "[线程 " << std::this_thread::get_id() << "] 客人退出" << std::endl;
                        close(client_fd);
                    } else {
                        std::cerr << "读取错误" << std::endl;
                        close(client_fd);
                    }
                }); // 结束 pool.enqueue
            }
        } // 结束 for 循环
    } // 结束 while 循环

      close(server_fd); // 关闭服务器套接字AS
      close(epoll_fd); // 关闭epoll实例
      return 0;

}