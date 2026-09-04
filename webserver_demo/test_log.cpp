#include"AsyncLogger.h"
#include<iostream>
#include<unistd.h>
int main(){
    std::cout<<"Starting logging test..."<<std::endl;
    for(int i=1;i<=50;i++){
      LOG_INFO("正常日志测试 %d/50",i);
    }
    for(int i=1;i<=50;i++){
      LOG_INFO("异常日志测试 %d/50",i);
    }
    std::cout<<"正在休眠5秒钟以确保日志写入完成..."<<std::endl;
    sleep(5);
    for(int i=1;i<=50;i++){
      LOG_INFO("追加日志测试 %d/",i);
    }
    std::cout<<"日志测试完成，请检查日志文件"<<std::endl;   
    return 0;
}