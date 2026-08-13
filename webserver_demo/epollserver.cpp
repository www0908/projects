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
#include<mutex>
#include "Threadpool.h"
#define PORT 8080
const int MAX_EVENTS = 1024; // 最大事件数
const int BUFFER_SIZE = 1024;//定义缓冲区大小
enum CheckState { STATE_REQUEST_LINE = 0, STATE_HEADER, STATE_FINISH };
enum HttpCode { NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST };
class HttpParser{
      private:
      CheckState current_state;
      public:
      HttpParser(){
        current_state =STATE_REQUEST_LINE;
      }
      HttpCode parse(std::string& buffer){
        size_t line_end =0;
        while((line_end =buffer.find("\r\n"))!=std::string::npos){
            std::string line =buffer.substr(0,line_end);
            buffer.erase(0,line_end+2);
            switch (current_state){
                case STATE_REQUEST_LINE:{
                    if(line.empty()) return BAD_REQUEST;
                    std::cout<<"提取成功"<<line<<std::endl;
                    current_state = STATE_HEADER;
                    break;
                }
                case STATE_HEADER:{
                    if(line.empty()){
                        std::cout<<"http报文全部解析完成\n"<<std::endl;
                        current_state =STATE_FINISH;
                        return GET_REQUEST;

                    }
                    std::cout<<"提取成功"<<line<<std::endl;
                    break;
                }
                default: 
                break;

            }
              
        }
        return NO_REQUEST;
      }
};
struct HttpConnection {
    HttpParser parser;       // 专属的状态机
    std::string buffer;      // 喂给状态机消化用的数据
    std::string full_data;   // 用于存储完整的HTTP请求数据
};
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
std::ostringstream buffer;
buffer<<file.rdbuf();
return buffer.str();
}
int main(){
    ThreadPool pool(4);
    std::unordered_map<int,HttpConnection> connection; // 用于存储每个客户端的解析器实例
    std::mutex connection_mutex; // 保护 connection 的互斥锁
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
                // 【前台接待】
                sockaddr_in client_addr{};
                socklen_t client_addr_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
                
                if(client_fd < 0){
                    std::cerr << "accept 错误\n" << std::endl;
                    continue;
                }
                
                set_nonblocking(client_fd);
                struct epoll_event ev{};
                ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT; 
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                
                // 【真正修复 1：客人进门，加锁建档！】
                {
                    std::lock_guard<std::mutex> lock(connection_mutex);
                    connection[client_fd] = HttpConnection(); 
                }

                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                std::cout << "新客户端连接，IP: " << client_ip << " 端口: " << ntohs(client_addr.sin_port) << std::endl;
           
            } else if(events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                
                // 【真正修复 2：把档案册和锁全部传进后厨！】
                pool.enqueue([client_fd, epoll_fd, &router, &connection, &connection_mutex]() {
                    char temp_buffer[2048];
                    memset(temp_buffer, 0, sizeof(temp_buffer));
                    
                    int bytes_read = read(client_fd, temp_buffer, sizeof(temp_buffer));
                    
                    if(bytes_read > 0) {
                        HttpCode parse_result;
                        std::string ready_to_parse_data;

                        // 【真正修复 3：加锁翻开客人的档案，喂给状态机！】
                        {
                            std::lock_guard<std::mutex> lock(connection_mutex);
                            connection[client_fd].buffer.append(temp_buffer, bytes_read);
                            connection[client_fd].full_data.append(temp_buffer, bytes_read);
                            
                            // 状态机开始吞吐数据
                            parse_result = connection[client_fd].parser.parse(connection[client_fd].buffer);
                            
                            if (parse_result == GET_REQUEST) {
                                ready_to_parse_data = connection[client_fd].full_data;
                            }
                        } // 自动解锁

                        // 【真正修复 4：根据状态机的裁判结果做事】
                        if (parse_result == NO_REQUEST) {
                            // 没收完，重新挂上雷达继续等
                            struct epoll_event ev{};
                            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                            ev.data.fd = client_fd;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev);
                            return; 
                        } 
                        else if (parse_result == BAD_REQUEST) {
                            close(client_fd);
                            std::lock_guard<std::mutex> lock(connection_mutex);
                            connection.erase(client_fd);
                            return;
                        } 
                        else if (parse_result == GET_REQUEST) {
                            // --- 数据彻底收齐！调用你原来的解析逻辑提取路径 ---
                            std::string path = parse_http_request(ready_to_parse_data.c_str(), ready_to_parse_data.length()); 
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

                            std::string http_response = 
                                status_line + 
                                "Content-Type: text/html; charset=utf-8\r\n" 
                                "Content-Length: " + std::to_string(html_body.length()) + "\r\n"
                                "Connection: close\r\n\r\n" + 
                                html_body;
                            
                            send(client_fd, http_response.c_str(), http_response.length(), 0);
                            
                            close(client_fd); 
                            
                            // 【极其关键：销户！】
                            {
                                std::lock_guard<std::mutex> lock(connection_mutex);
                                connection.erase(client_fd); 
                            }
                        }

                    } else { 
                        // 读数据失败或对方断开连接
                        close(client_fd);
                        std::lock_guard<std::mutex> lock(connection_mutex);
                        connection.erase(client_fd);
                    }
                });
            }
        }
    }
      close(server_fd); // 关闭服务器套接字AS
      close(epoll_fd); // 关闭epoll实例
      return 0;

}