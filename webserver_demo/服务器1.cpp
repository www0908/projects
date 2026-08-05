#include<iostream>
#include<sys/socket.h>
#include<netinet/in.h>
#include<unistd.h>
#include<cstring>
int main(){
    int server_fd= socket(AF_INET,SOCK_STREAM,0);
    if(server_fd == -1){
        std::cerr<<"创建 SOcket失败"<<std::endl;
        return -1;
    }
sockaddr_in address;
std::memset(&address,0,sizeof(address));
address.sin_family =AF_INET;
address.sin_addr.s_addr=INADDR_ANY;
address.sin_port=htons(8080);
if(bind(server_fd,(struct sockaddr*)&address,sizeof(address))<0){
    std::cerr<<"绑定端口8080失败"<<std::endl;
    close(server_fd);
    return -1;
}
if(listen(server_fd,10)<0){
    std::cerr<<"监听端口8080失败"<<std::endl;
    close(server_fd);
    return -1;

}
std::cout<<"服务器启动成功，正在监听端口8080..."<<std::endl;
int addrlen=sizeof(address);
int client_fd=accept(server_fd,(struct sockaddr*)&address,(socklen_t*)&addrlen);
if(client_fd<0){
    std::cerr<<"接受客户端连接失败"<<std::endl;
    close(server_fd);
    return -1;
}
std::cout<<"客户端连接成功"<<std::endl;
close(client_fd);
close(server_fd);
return 0;
}