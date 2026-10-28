#include <iostream>
#include <cstring>
#include <unistd.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <string>
#include <fcntl.h>
#include <sstream>
#include <unordered_map>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fstream>
#include <cerrno>
#include <mutex>
#include <string_view>
#include "Threadpool.h"
#include <queue>
#include <chrono>
#include <algorithm>
#include "buffer.h"
#include "AsyncLogger.h"
#include "sqlConnPool.h"
#include "crypto_util.h"
#include <sys/uio.h> // writev 需要用到
#include<cstdlib>
#include<cctype>
#include<cerrno>

#define PORT 8080

std::unordered_map<std::string,std::string> ParsePostFormData(const std::string& body){
    std::unordered_map<std::string,std::string> post_data;
    size_t start=0;
    size_t end=0;
    while((end=body.find('&',start))!=std::string::npos){
        std::string pair=body.substr(start,end-start);
        size_t equal_pos=pair.find('=');
        if(equal_pos!=std::string::npos){
            std::string key=pair.substr(0,equal_pos);
            std::string value=pair.substr(equal_pos+1);
            post_data[key]=value;
        }
        start=end+1;
    }
  std::string last_pair=body.substr(start);
  size_t equal_pos=last_pair.find('=');
    if(equal_pos!=std::string::npos){
        std::string key=last_pair.substr(0,equal_pos);
        std::string value=last_pair.substr(equal_pos+1);
        post_data[key]=value;
    }
    return post_data;
}

bool VerifyUser(const std::string& name,const std::string& pwd,bool isLogin){
    if(name.empty()||pwd.empty()){
        return false;
    }
    MYSQL* sql=nullptr;
    SqlConnRAII raii(&sql,SqlConnPool::Instance());
    if(!sql){
        return false;
    }
    std::string pwd_hash =sha256(pwd);
    bool flag=false;
    MYSQL_STMT* stmt=mysql_stmt_init(sql);
    if(!stmt) return false;
    const char* query="SELECT password FROM users where username=? limit 1";
    if(mysql_stmt_prepare(stmt,query,strlen(query))!=0){
        LOG_ERROR("{DB ERROR} stmt prepare 失败：%s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }
    MYSQL_BIND bind_param[1];
    memset(bind_param,0,sizeof(bind_param));
    unsigned long name_len=name.length();
    bind_param[0].buffer_type=MYSQL_TYPE_STRING;
    bind_param[0].buffer=const_cast<char*>(name.c_str());
    bind_param[0].buffer_length=name_len;
    bind_param[0].length=&name_len;
    mysql_stmt_bind_param(stmt,bind_param);
    
    if (mysql_stmt_execute(stmt) == 0) {
        mysql_stmt_store_result(stmt);
        int rows = mysql_stmt_num_rows(stmt);

        if (rows > 0) {
            // 用户已存在
            if (isLogin) {
                // 登录分支：提取数据库中已存的 64 位哈希值
                char db_pwd_hash[65] = {0};
                unsigned long out_len = 0;
                MYSQL_BIND bind_result[1];
                memset(bind_result, 0, sizeof(bind_result));

                bind_result[0].buffer_type = MYSQL_TYPE_STRING;
                bind_result[0].buffer = db_pwd_hash;
                bind_result[0].buffer_length = sizeof(db_pwd_hash);
                bind_result[0].length = &out_len;

                mysql_stmt_bind_result(stmt, bind_result);

                if (mysql_stmt_fetch(stmt) == 0) {
                    // 比对哈希串
                    if (pwd_hash == std::string(db_pwd_hash)) {
                        flag = true;
                    }
                }
            } else {
                // 注册分支：用户名已存在，注册失败
                flag = false;
            }
        } else {
            // 用户不存在
            if (!isLogin) {
                // 注册分支：执行安全插入
                mysql_stmt_close(stmt); // 先关闭查询的 stmt
                stmt = mysql_stmt_init(sql);

                const char* insert_sql = "INSERT INTO users(username, password) VALUES(?, ?)";
                if (mysql_stmt_prepare(stmt, insert_sql, strlen(insert_sql)) == 0) {
                    MYSQL_BIND insert_bind[2];
                    memset(insert_bind, 0, sizeof(insert_bind));

                    // 绑定 username
                    insert_bind[0].buffer_type = MYSQL_TYPE_STRING;
                    insert_bind[0].buffer = const_cast<char*>(name.c_str());
                    insert_bind[0].buffer_length = name_len;
                    insert_bind[0].length = &name_len;

                    // 绑定加盐哈希后的 password
                    unsigned long hash_len = pwd_hash.length();
                    insert_bind[1].buffer_type = MYSQL_TYPE_STRING;
                    insert_bind[1].buffer = const_cast<char*>(pwd_hash.c_str());
                    insert_bind[1].buffer_length = hash_len;
                    insert_bind[1].length = &hash_len;

                    mysql_stmt_bind_param(stmt, insert_bind);
                    if (mysql_stmt_execute(stmt) == 0) {
                        flag = true; // 注册落库成功
                    }
                }
            }
        }
    }

    // 释放预处理句柄
    mysql_stmt_close(stmt);
    return flag;
}

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

enum CheckState { STATE_REQUEST_LINE = 0, STATE_HEADER, STATE_BODY,STATE_FINISH };
enum HttpCode { NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST };

class MmapFile{
public:
    char* file_ptr;
    size_t file_size;
    
    MmapFile(const std::string& filepath): file_ptr(nullptr),file_size(0){
        int fd = open(filepath.c_str(), O_RDONLY);
        if(fd < 0) return;
        struct stat stat_buf;
        if(fstat(fd, &stat_buf) == 0){
            file_size = stat_buf.st_size;
            file_ptr = (char*)mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if(file_ptr == MAP_FAILED){
                file_ptr = nullptr;
                file_size = 0;
            }
        }
        close(fd);
    }
  
    ~MmapFile(){
        if(file_ptr != nullptr && file_size > 0){
            munmap(file_ptr, file_size);
        }
    }
};

class HttpParser{
private:
    CheckState current_state;
    int content_length_;
    int parsed_header_bytes_;
    bool content_length_seen_;
public:
    std::string request_path;
    std::string method;
    std::string body_data;
    bool is_keep_alive_ ;
      
    HttpParser(){
       reset();
    }
      
    void reset(){
        current_state = STATE_REQUEST_LINE;
        request_path.clear();
        method.clear();
        body_data.clear();
        content_length_ = 0;
        parsed_header_bytes_=0;
        content_length_seen_=0;
        is_keep_alive_=false;
    }
      
    HttpCode parse(Buffer& buffer){
        while (buffer.ReadableBytes() > 0){
            if(current_state == STATE_BODY){
                if(buffer.ReadableBytes() < content_length_){
                    return NO_REQUEST;
                }
                body_data = buffer.RetrieveToStr(content_length_);
                current_state = STATE_FINISH;
                return GET_REQUEST;
            }
            const char* crlf = std::search(buffer.Peek(), buffer.BeginWriteConst(), "\r\n", "\r\n"+2);
            
            if(crlf == buffer.BeginWriteConst()){
                 if (buffer.ReadableBytes() > 65536) {
                return BAD_REQUEST; 
            }
                return NO_REQUEST;
            }
            int current_line_len = crlf - buffer.Peek() + 2;
            parsed_header_bytes_ += current_line_len;
            if (parsed_header_bytes_ > 65536) {
            return BAD_REQUEST; // 头部累积超限，果断拒绝
           }
            
            std::string line(buffer.Peek(), crlf);
            buffer.RetrieveUntil(crlf + 2);
            
            switch (current_state){
                case STATE_REQUEST_LINE:{
                    if(line.empty()) return BAD_REQUEST;
                    size_t space1=line.find(' ');
                    if(space1 == std::string::npos) return BAD_REQUEST;
                    size_t space2=line.find(' ',space1+1);
                    if(space2 == std::string::npos) return BAD_REQUEST;
                    this->method=line.substr(0,space1);
                    this->request_path=line.substr(space1+1,space2-space1-1);
                    std::string version=line.substr(space2+1);
                    //检验方法
                    if(this->request_path.empty()||this->request_path[0]!='/'){
                        return BAD_REQUEST;
                    }
                    if (this->request_path.empty() || this->request_path[0] != '/') {
                        return BAD_REQUEST;
                    }
                    if(version!="HTTP/1.1"&&version!="HTTP/1.0"){
                        return BAD_REQUEST;
                    }
                    current_state=STATE_HEADER;
                    break;
                }
                case STATE_HEADER:{
                    if(line.empty()){
                        if(content_length_ > 0){
                            current_state = STATE_BODY;
                            break;
                        }
                        else{
                            current_state = STATE_FINISH;
                            return GET_REQUEST;
                        }
                    }
                    size_t colon_pos=line.find(':');
                    if(colon_pos== std::string::npos){
                        return BAD_REQUEST;

                    }
                    std::string key = line.substr(0, colon_pos);
                    std::string value = line.substr(colon_pos + 1);
                    size_t value_start = value.find_first_not_of(" \t");
                    if(value_start!=std::string::npos){
                        value=value.substr(value_start);
                    }
                    else{
                        value.clear();
                    }
                    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                       if (key == "content-length") {
                    // 为什么要这样写：用独立 bool 拦截重复，即使前一个是 Content-Length: 0 也能精准捕获。
                       if (content_length_seen_) return BAD_REQUEST;
                      content_length_seen_ = true;
                       if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                        return BAD_REQUEST;
                          } 
                          errno=0;
                          char* end_ptr = nullptr;
                          long long len = std::strtoll(value.c_str(), &end_ptr, 10);

                          if(errno==ERANGE||len < 0||*end_ptr !='\0'){
                            return BAD_REQUEST;
                          }
                          if(len>10*1024*1024){
                            return BAD_REQUEST;
                          }
                          content_length_=static_cast<int>(len);
                    }
                    else if (key == "transfer-encoding") {
                    // 容易踩的坑：不能只判断 chunked。只要出现这个头，说明客户端想用某种编码，我们当前不支持，必须无差别拒绝，否则会导致后续报文边界错乱。
                    return BAD_REQUEST;
                          }
                   else if (key == "connection") {
                    // 只在需要比较协议 Token 时，按需转小写
                    std::string lower_value = value;
                    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), [](unsigned char c) { return std::tolower(c); });
                    
                    if (lower_value.find("keep-alive") != std::string::npos) {
                        this->is_keep_alive_ = false; // 当前第一轮强制降级短连接
                    }
                }
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
    Buffer readBuff;      // 喂给状态机消化用的数据
    Buffer writeBuff;
    std::string full_data;   // 用于存储完整的HTTP请求数据
    long long last_expire_time; // 用于存储最后过期时间
    struct iovec iov[2];
    int iov_cnt;
    size_t bytes_to_send;
    size_t bytes_have_send;
    char* file_ptr;
    size_t file_size;
    HttpConnection() : readBuff(1024), writeBuff(1024), last_expire_time(0),iov_cnt(0), bytes_to_send(0),bytes_have_send(0), file_ptr(nullptr), file_size(0) {}
    ~HttpConnection(){
        UnmapFile();
    }
    void UnmapFile(){
        if(file_ptr!=nullptr&&file_size>0){
            munmap(file_ptr,file_size);
            file_ptr=nullptr;
            file_size=0;
        }
    }
    bool MapFile(const std::string& filepath){
        UnmapFile(); // 映射新文件前，先把老文件清理了防内存泄露
        int fd = open(filepath.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat stat_buf;
        if (fstat(fd, &stat_buf) == 0) {
            file_size = stat_buf.st_size;
            file_ptr = (char*)mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (file_ptr == MAP_FAILED) {
                file_ptr = nullptr;
                file_size = 0;
                close(fd);
                return false;
            }
        } else {
            close(fd);
            return false;
        }
        close(fd);
        return true;
    }
    bool Read(int fd) {
    int saveErrno = 0;
    while (true) {
        ssize_t len = readBuff.ReadFd(fd, &saveErrno);
        if (len > 0) {
            continue; // 只要能读就继续榨干缓冲区
        } else if (len == 0) {
            return false; // 对端关闭
        } else {
            if (saveErrno == EINTR) continue; // 假中断，重试
            if (saveErrno == EAGAIN || saveErrno == EWOULDBLOCK) {
                break; // 榨干了，挂起交还控制权
            }
            return false; // 致命错误
        }
    }
    return true;
}
    bool Write(int fd) {
    int saveErrno = 0;
    while (true) {
        ssize_t len = writeBuff.WriteFd(fd, &saveErrno);
        if (len > 0) {
            if (writeBuff.ReadableBytes() == 0) return true; // 发送完毕
            continue; 
        } else if (len == 0) {
            return false;
        } else {
            if (saveErrno == EINTR) continue;
            if (saveErrno == EAGAIN || saveErrno == EWOULDBLOCK) {
                return true; // 发送缓冲区满，挂起等待下次 EPOLLOUT
            }
            return false;
        }
    }
}
    void ClearResponseState() {
        UnmapFile(); // 彻底发完一个网页后，主动释放内存
        writeBuff.RetrieveAll();
        iov_cnt = 0;
        bytes_to_send = 0;
        bytes_have_send = 0;
    }
    bool PrepareIovForRemaining() {
        const size_t header_size = writeBuff.ReadableBytes();

        if (bytes_have_send >= bytes_to_send) {
            iov_cnt = 0;
            return true;
        }

        // 响应头还没发完
        if (bytes_have_send < header_size) {
            iov[0].iov_base = const_cast<char*>(writeBuff.Peek()) + bytes_have_send;
            iov[0].iov_len = header_size - bytes_have_send;

            if (file_size > 0) {
                iov[1].iov_base = file_ptr;
                iov[1].iov_len = file_size;
                iov_cnt = 2;
            } else {
                iov_cnt = 1;
            }
        } 
        // 响应头已发完，纯发文件内容
        else {
            const size_t file_offset = bytes_have_send - header_size;
            iov[0].iov_base = file_ptr + file_offset;
            iov[0].iov_len = file_size - file_offset;
            iov_cnt = 1;
        }
        

        return true;
    }
    // 返回 true 表示响应已全部发送；false 表示遇到 EAGAIN，等待 EPOLLOUT
bool FlushResponse(int client_fd, int& saved_errno) {
    while (bytes_have_send < bytes_to_send) {
        PrepareIovForRemaining();

        ssize_t n = writev(client_fd, iov, iov_cnt);
        if (n > 0) {
            bytes_have_send += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            saved_errno = errno;
            return false;
        }

        saved_errno = (n == 0) ? EPIPE : errno;
        return false;
    }

    return true;
}
}; // 结构体结束


void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);// 获取文件描述符的当前标志
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);// 设置非阻塞模式
}

int main(){
    // 初始化数据库连接池
    SqlConnPool::Instance()->Init("127.0.0.1", 3306, "root", "123456", "webserver_db", 10);
    ThreadPool pool(4);
    
    std::unordered_map<int,std::shared_ptr<HttpConnection>> connection; // 用于存储每个客户端的解析器实例
    std::mutex connection_mutex; // 保护 connection 的互斥锁
    
    std::unordered_map<std::string, std::string> router = {
        {"/","index.html"},
        {"/study/linux/tcp","tcp.html"},
        {"/user","user.html"},
        {"/error.html", "error.html"}
    };
    
    std::priority_queue<TimerNode> timer_heap; // 用于存储定时器节点的优先队列
    std::mutex timer_mutex; // 保护 timer_heap 的互斥锁
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("创建 Server Socket 失败! errno: %d", errno);
        return -1; // 初始化失败直接退出，不留后患
    }
    
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        LOG_ERROR("设置端口复用失败! errno: %d", errno);
        close(server_fd);
        return -1;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8080);
    
   if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("绑定端口失败! errno: %d", errno);
        close(server_fd); // 失败路径必须清理之前创建的 fd
        return -1;
    }
    if (listen(server_fd, 1024) < 0) { // 1024 是全连接队列长度
        LOG_ERROR("监听端口失败! errno: %d", errno);
        close(server_fd);
        return -1;
    }
    int epoll_fd = epoll_create1(0);
    if(epoll_fd<0){
        LOG_ERROR("创建EPOLL实例失败！ errno:%d",errno);
        close(server_fd);
        return -1;

    }
    int old_option =fcntl (server_fd,F_GETFL);
    if(old_option<0 || fcntl(server_fd,F_SETFL, old_option | O_NONBLOCK)<0){
        LOG_ERROR("设置 server_fd 非阻塞失败! errno: %d", errno);
        close(epoll_fd);
        close(server_fd);
        return -1;
    }

    
   
    
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
                TimerNode top_timer = timer_heap.top();
                if(top_timer.expire_time > current_time){
                    break;
                }
                timer_heap.pop();
                if(connection.find(top_timer.fd) != connection.end()){
                    if(connection[top_timer.fd]->last_expire_time == top_timer.expire_time){
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, top_timer.fd, nullptr);
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
                    connection[client_fd] = std::make_shared<HttpConnection>();
                    connection[client_fd]->last_expire_time = expire;
                }
            
                {
                    std::lock_guard<std::mutex> t_lock(timer_mutex);
                    timer_heap.push({client_fd, expire});
                }
            } 
           // ✅ 修复了极其致命的优先级括号问题
           // ✅ 修复了极其致命的优先级括号问题，并补齐了全部变量捕获
            else if (events[i].events & (EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP)) {
                const int client_fd = events[i].data.fd;
                const uint32_t ready_events = events[i].events;
                
                // 【后厨处理业务】
                EnqueueResult res = pool.AddTask([client_fd, ready_events, epoll_fd, &connection, &connection_mutex, &router, &timer_heap, &timer_mutex]() {
                    // 【第一道防线：异常拦截】
                    if (ready_events & (EPOLLERR | EPOLLHUP)) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                        close(client_fd);
                        std::lock_guard<std::mutex> lock(connection_mutex);
                        connection.erase(client_fd);
                        return; 
                    }
                    
                    // 【第二道防线：安全获取连接】
                    std::shared_ptr<HttpConnection> conn_ptr;
                    {
                        std::lock_guard<std::mutex> lock(connection_mutex);
                        auto it = connection.find(client_fd);
                        if (it == connection.end()) return;
                        conn_ptr = it->second;
                    }
                    
                    bool is_read = (ready_events & EPOLLIN);
                    bool is_write = (ready_events & EPOLLOUT);

                    // ==========================================
                    // 轨道 1：EPOLLIN（处理新发来的请求）
                    // ==========================================
                    if (is_read) {
                        int saveErrno = 0;
                        ssize_t bytes_read = conn_ptr->readBuff.ReadFd(client_fd, &saveErrno);
                        
                        if (bytes_read > 0) {
                            while (conn_ptr->parser.parse(conn_ptr->readBuff) == GET_REQUEST) {
                                std::string path = conn_ptr->parser.request_path; 
                                std::string method = conn_ptr->parser.method;
                                std::string status_line, html_body;
                                
                                // POST 逻辑
                                if (method == "POST") {
                                    auto post_data = ParsePostFormData(conn_ptr->parser.body_data);
                                    std::string user = post_data["username"];
                                    std::string pwd = post_data["password"];
                                    if (path == "/login") {
                                        path = VerifyUser(user, pwd, true) ? "/" : "/error.html";
                                    } else if (path == "/register") {
                                        path = VerifyUser(user, pwd, false) ? "/user" : "/error.html";
                                    }
                                }
                                
                                // 组装报文逻辑
                                if (path.empty()) {
                                    status_line = "HTTP/1.1 400 Bad Request\r\n";
                                    std::string error_body = "<html><body><h1>400 Bad Request</h1><p>请求不合法</p></body></html>";
                                    std::string header = status_line + 
                                                         "Content-Type: text/html; charset=utf-8\r\n"
                                                         "Content-Length: " + std::to_string(error_body.length()) + "\r\n"
                                                         "Connection: keep-alive\r\n\r\n" + error_body;
                                    conn_ptr->writeBuff.Append(header);
                                    conn_ptr->iov[0].iov_base = const_cast<char*>(conn_ptr->writeBuff.Peek());
                                    conn_ptr->iov[0].iov_len = conn_ptr->writeBuff.ReadableBytes();
                                    conn_ptr->iov_cnt = 1;
                                    conn_ptr->file_size = 0;
                                } else {
                                    auto it = router.find(path);
                                    if (it != router.end()) {
                                        std::string filepath = it->second;
                                        if (!conn_ptr->MapFile(filepath)) {
                                            status_line = "HTTP/1.1 404 Not Found\r\n";
                                            std::string error_body = "<html><body><h1>404 Not Found</h1></body></html>";
                                            std::string header = status_line + 
                                                                 "Content-Type: text/html; charset=utf-8\r\n"
                                                                 "Content-Length: " + std::to_string(error_body.length()) + "\r\n"
                                                                 "Connection: keep-alive\r\n\r\n" + error_body;
                                            conn_ptr->writeBuff.Append(header);
                                            conn_ptr->iov[0].iov_base = const_cast<char*>(conn_ptr->writeBuff.Peek());
                                            conn_ptr->iov[0].iov_len = conn_ptr->writeBuff.ReadableBytes();
                                            conn_ptr->iov_cnt = 1;
                                            conn_ptr->file_size = 0;
                                        } else {
                                            status_line = "HTTP/1.1 200 OK\r\n";
                                            std::string header = status_line + 
                                                                 "Content-Type: text/html; charset=utf-8\r\n"
                                                                 "Content-Length: " + std::to_string(conn_ptr->file_size) + "\r\n"
                                                                 "Connection: keep-alive\r\n\r\n";
                                            conn_ptr->writeBuff.Append(header);
                                            
                                            conn_ptr->iov[0].iov_base = const_cast<char*>(conn_ptr->writeBuff.Peek());
                                            conn_ptr->iov[0].iov_len = conn_ptr->writeBuff.ReadableBytes();
                                            conn_ptr->iov[1].iov_base = conn_ptr->file_ptr;
                                            conn_ptr->iov[1].iov_len = conn_ptr->file_size;
                                            conn_ptr->iov_cnt = 2;
                                        }
                                    } else {
                                        status_line = "HTTP/1.1 404 Not Found\r\n";
                                        std::string error_body = "<html><body><h1>404 Not Found</h1></body></html>";
                                        std::string header = status_line + 
                                                             "Content-Type: text/html; charset=utf-8\r\n"
                                                             "Content-Length: " + std::to_string(error_body.length()) + "\r\n"
                                                             "Connection: keep-alive\r\n\r\n" + error_body;
                                        conn_ptr->writeBuff.Append(header);
                                        conn_ptr->iov[0].iov_base = const_cast<char*>(conn_ptr->writeBuff.Peek());
                                        conn_ptr->iov[0].iov_len = conn_ptr->writeBuff.ReadableBytes();
                                        conn_ptr->iov_cnt = 1;
                                        conn_ptr->file_size = 0;
                                    }
                                }

                                // 统一初始化发送参数，调用 FlushResponse
                                conn_ptr->bytes_have_send = 0;
                                conn_ptr->bytes_to_send = conn_ptr->writeBuff.ReadableBytes() + conn_ptr->file_size;
                                
                                int write_errno = 0;
                                if (!conn_ptr->FlushResponse(client_fd, write_errno)) {
                                    if (write_errno == EAGAIN || write_errno == EWOULDBLOCK) {
                                        struct epoll_event ev{};
                                        ev.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
                                        ev.data.fd = client_fd;
                                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev);
                                        return; 
                                    } else {
                                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                        close(client_fd);
                                        std::lock_guard<std::mutex> lock(connection_mutex);
                                        connection.erase(client_fd);
                                        return;
                                    }
                                }

                                conn_ptr->ClearResponseState();

                                // 重置状态机，更新定时器
                                conn_ptr->parser.reset(); 
                                long long new_expire = get_current_time() + 10000; 
                                conn_ptr->last_expire_time = new_expire;
                                {
                                    std::lock_guard<std::mutex> t_lock(timer_mutex);
                                    timer_heap.push({client_fd, new_expire});   
                                }
                            } // while 结束

                            struct epoll_event ev{};
                            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT; 
                            ev.data.fd = client_fd;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev); 
                        }
                        else if (bytes_read == 0 || (bytes_read < 0 && saveErrno != EAGAIN)) { 
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            close(client_fd);
                            std::lock_guard<std::mutex> lock(connection_mutex);
                            connection.erase(client_fd);
                        }
                    }
                    // ==========================================
                    // 轨道 2：EPOLLOUT（专供大文件断点补发的通道）
                    // ==========================================
                    else if (is_write) {
                        int write_errno = 0;

                        if (!conn_ptr->FlushResponse(client_fd, write_errno)) {
                            if (write_errno == EAGAIN || write_errno == EWOULDBLOCK) {
                                struct epoll_event ev{};
                                ev.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
                                ev.data.fd = client_fd;
                                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev) < 0) {
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                    close(client_fd);
                                    std::lock_guard<std::mutex> lock(connection_mutex);
                                    connection.erase(client_fd);
                                }
                                return;
                            }

                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            close(client_fd);
                            std::lock_guard<std::mutex> lock(connection_mutex);
                            connection.erase(client_fd);
                            return;
                        }

                        conn_ptr->ClearResponseState();

                        struct epoll_event ev{};
                        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                        ev.data.fd = client_fd;
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev) < 0) {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            close(client_fd);
                            std::lock_guard<std::mutex> lock(connection_mutex);
                            connection.erase(client_fd);
                        }
                    }
                }); // <--- 后厨 Lambda 到此结束

                // ==========================================
                // 【前台接盘：处理线程池拒单，执行熔断拔管】
                // ==========================================
                if (res == EnqueueResult::QUEUE_FULL) {
                    static int dropCount = 0;
                    if (++dropCount % 100 == 1) { 
                        LOG_ERROR("线程池满载，触发限流熔断！累计丢弃: %d 个请求", dropCount);
                    }
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                    close(client_fd);
                    std::lock_guard<std::mutex> lock(connection_mutex);
                    connection.erase(client_fd);
                } 
                else if (res == EnqueueResult::POOL_CLOSED) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                    close(client_fd);
                    std::lock_guard<std::mutex> lock(connection_mutex);
                    connection.erase(client_fd);
                }
            }
    close(server_fd); 
    close(epoll_fd); 
    return 0;
}
}
        