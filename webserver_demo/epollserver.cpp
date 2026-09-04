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
#include<string_view>
#include "Threadpool.h"
#include<queue>
#include<chrono>
#include"AsyncLogger.h"
#define PORT 8080
const int MAX_EVENTS = 1024; // 最大事件数
const int BUFFER_SIZE = 1024;//定义缓冲区大小
long long get_current_time() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}
struct TimerNode{
    int fd;
    long long expire_time;
    bool operator<(const TimerNode& other) const {
        return expire_time > other.expire_time; // 过期时间越小，优先级越高
    }
};
enum CheckState { STATE_REQUEST_LINE = 0, STATE_HEADER, STATE_FINISH };
enum HttpCode { NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST };
class HttpParser{
      private:
      CheckState current_state;
      public:
      std::string request_path;
      HttpParser(){
        current_state =STATE_REQUEST_LINE;
      }
      void reset(){
        current_state = STATE_REQUEST_LINE;
        request_path.clear();
      }
      HttpCode parse(std::string& buffer){
        size_t line_end =0;
        while((line_end =buffer.find("\r\n"))!=std::string::npos){
            std::string line =buffer.substr(0,line_end);
            buffer.erase(0,line_end+2);
            switch (current_state){
                case STATE_REQUEST_LINE:{
                    if(line.empty()) return BAD_REQUEST;
                    //LOG_INFO("提取成功: %s", line.c_str());
                    std::istringstream iss(line);
                    std::string method, version;
                    iss >> method >> request_path >> version;
                    current_state = STATE_HEADER;
                    break;
                }
                case STATE_HEADER:{
                    if(line.empty()){
                       
                        
                        return GET_REQUEST;

                    }
                    //LOG_INFO("提取成功: %s", line.c_str());
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
    long long last_expire_time; // 用于存储最后过期时间
};
// std::string parse_http_request(const char* buffer,ssize_t bytes_recevied){
//     std::string_view raw_request(buffer,bytes_recevied);
//     size_t line_end =raw_request.find("\r\n");
//     if(line_end==std::string_view::npos){
//         line_end=raw_request.find('n');
//         if(line_end==std::string_view::npos){
//             return "";
//         }

//     }
//     std::string_view first_line=raw_request.substr(0,line_end);
//     return std::string(first_line);


// }
// void parse_http_request(const char* buffer){
//     std::string raw_request(buffer);
//     size_t line_end = raw_request.find("\r\n"); // 查找请求行结束符
//     if(line_end == std::string::npos){// 检查是否找到行结束符
//         LOG_ERROR("无效的HTTP请求");
//         return;
//     }
//     std::string request_line =raw_request.substr(0,line_end);// 提取请求行
//     std::istringstream request_stream(request_line);// 使用字符串流解析请求行
//     std::string method, path, version;// 定义变量存储请求方法、路径和HTTP版本
//     if(request_stream>>method>>path>>version){
//         LOG_INFO("请求方法: %s", method.c_str());
//         LOG_INFO("请求路径: %s", path.c_str());
//         LOG_INFO("HTTP版本: %s", version.c_str());
//     }
// }
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);// 获取文件描述符的当前标志
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);// 设置非阻塞模式
}
std::string read_file_content (const std::string&filepath){
std::ifstream file(filepath);
if(!file.is_open()){
    LOG_ERROR("无法打开文件: %s", filepath.c_str());
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
    std::priority_queue<TimerNode,std::vector<TimerNode>,std::greater<TimerNode>> timer_heap; // 定义一个优先队列，用于存储定时器节点
    std::mutex timer_mutex; // 保护 timer_heap 的互斥锁
     int epoll_fd = epoll_create1(0);// 创建epoll实例
    int server_fd =socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0){
        LOG_ERROR("Socket 创建失败");
        return -1;
    }
    int opt=1;//设置套接字选项
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));// 设置套接字选项，允许地址重用
    sockaddr_in address{};//定义服务器地址结构体
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;// 绑定到所有可用的网络接口
    address.sin_port = htons(PORT);// 将端口号转换为网络字节序
    if(bind(server_fd,(struct sockaddr*)&address,sizeof(address))<0){
        LOG_ERROR("绑定失败");
        return -1;
    }
    if(listen(server_fd,5)<0){
        LOG_ERROR("监听失败");
        return -1;
    }
    set_nonblocking(server_fd);// 设置服务器套接字为非阻塞模式
    struct epoll_event ev{};
    ev.events = EPOLLIN; // 设置事件类型为可读事件
    ev.data.fd = server_fd; // 将服务器套接字的文件描述符存
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev); // 将服务器套接字添加到epoll实例中
    struct epoll_event events[MAX_EVENTS]; // 用于存储就绪事件的数组
    LOG_INFO("epoll服务器已启动，监听端口 %d", PORT);
while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if(nfds == -1 && errno != EINTR) {
            LOG_ERROR("epoll_wait 错误");
            break;
        }
        {
            long long current_time = get_current_time();
           std::lock_guard<std::mutex> c_lock(connection_mutex);
           std::lock_guard<std::mutex> t_lock(timer_mutex);
            while(!timer_heap.empty()){
                TimerNode top_timer=timer_heap.top();
                if(top_timer.expire_time>current_time){
                    break;
                }
                timer_heap.pop();
                if(connection.find(top_timer.fd)!=connection.end()){
                    if(connection[top_timer.fd].last_expire_time==top_timer.expire_time){
                        epoll_ctl(epoll_fd,EPOLL_CTL_DEL,top_timer.fd,nullptr);
                        close(top_timer.fd);
                        connection.erase(top_timer.fd);
                    }
                }

            }
        }

        for(int i = 0; i < nfds; i++){
            int fd = events[i].data.fd;

            if(fd == server_fd) {
                // 【前台接待】
                
                sockaddr_in client_addr{};
                socklen_t client_addr_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
                
                if(client_fd < 0){
                    LOG_ERROR("accept 错误");
                    continue;
                }
                
                set_nonblocking(client_fd);
                struct epoll_event ev{};
                ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT; 
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

               long long expire = get_current_time() + 10000;

                {
                  std::lock_guard<std::mutex> c_lock(connection_mutex);
                   connection[client_fd] = HttpConnection();
                    connection[client_fd].last_expire_time = expire;
                }

                {
                   std::lock_guard<std::mutex> t_lock(timer_mutex);
                    timer_heap.push({client_fd, expire});
                  }
                {
                    std::lock_guard<std::mutex> t_lock(timer_mutex);
                    timer_heap.push({client_fd,expire});
                }
            //     // 【真正修复 1：客人进门，加锁建档！】
            //     {
            //         std::lock_guard<std::mutex> lock(connection_mutex);
            //         connection[client_fd] = HttpConnection(); 
            //     }

            //     char client_ip[INET_ADDRSTRLEN];
            //     inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            //     //std::cout << "新客户端连接，IP: " << client_ip << " 端口: " << ntohs(client_addr.sin_port) << std::endl;
           
            } 
                else if(events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                
                // 【真正修复 2：把档案册和锁全部传进后厨！】
                pool.enqueue([client_fd, epoll_fd, &router, &connection, &connection_mutex, &timer_mutex,&timer_heap]() {
                    char temp_buffer[2048];
                    memset(temp_buffer, 0, sizeof(temp_buffer));
                    
                    int bytes_read = read(client_fd, temp_buffer, sizeof(temp_buffer));
                    
                    if(bytes_read > 0) {
                        std::lock_guard<std::mutex> lock(connection_mutex);
                        connection[client_fd].buffer.append(temp_buffer, bytes_read);

                        // 【核心大杀器：只要安检员说收齐了，就一直往外发网页，绝对不 clear buffer！】
                        while (connection[client_fd].parser.parse(connection[client_fd].buffer) == GET_REQUEST) {
                            
                            std::string path = connection[client_fd].parser.request_path; 
                            std::string status_line, html_body;

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
                                        html_body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
                                    } else {
                                        status_line = "HTTP/1.1 200 OK\r\n";
                                    }
                                } else {
                                    status_line = "HTTP/1.1 404 Not Found\r\n";
                                    html_body = "<html><body><h1>404 Not Found</h1></body></html>";
                                }
                            }

                            std::string http_response = 
                                status_line + 
                                "Content-Type: text/html; charset=utf-8\r\n" 
                                "Content-Length: " + std::to_string(html_body.length()) + "\r\n"
                                "Connection: keep-alive\r\n\r\n" + 
                                html_body;
                            
                            send(client_fd, http_response.c_str(), http_response.length(), 0);
                            
                            // 网页发完了，安检员洗脑准备查下一单！
                            // 极其关键：因为我们没写 buffer.clear()，所以 buffer 里剩下的连发请求会被保留到下一个 while 循环！
                            connection[client_fd].parser.reset(); 
                            long long new_expire = get_current_time() + 10000; // 10秒后过期
                            connection[client_fd].last_expire_time = new_expire;
                            {
                                std::lock_guard<std::mutex> t_lock(timer_mutex);
                                timer_heap.push({client_fd, new_expire});   
                            }
                        
                        }

                        // 【第三步：拔掉 EPOLLET 的毒牙】
                        // 数据榨干了，重新把呼叫铃挂回大堂。注意：我把你原来的 EPOLLET 删了！
                        // 既然咱们没有用 while(read) 循环读到 EAGAIN，就老老实实用默认的水平触发(LT)最安全。
                        struct epoll_event ev{};
                        ev.events = EPOLLIN | EPOLLONESHOT; // 取消了 EPOLLET
                        ev.data.fd = client_fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev); 

                    } else if (bytes_read == 0) { // 这里不能和大于0混在一起，否则死锁
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