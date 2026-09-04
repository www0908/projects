#pragma once
#include <string>
#include <fstream>
#include<iostream>
#include<vector>
#include<mutex>
#include<thread>
#include<chrono>
#include<sstream>
#include<iomanip>
#include<cstdarg>
#include<condition_variable>
class AsyncLogger {
    private:
       std::vector<std::string> current_buffer;
       std::vector<std::string> next_buffer;
       std::mutex mtx;
         std::condition_variable cv;
         bool running;
         std::thread log_thread;
         AsyncLogger():running(true){
            log_thread = std::thread(&AsyncLogger::threadFunc, this);
         }
         void threadFunc(){

            std::ofstream file("server.log",std::ios::app);
            while(running){
                {std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock,std::chrono::seconds(3),[this]{
                return !next_buffer.empty()|| !running;
            });
            if (!running && next_buffer.empty()){
                break;
            }
            next_buffer.swap(current_buffer);
            
        }
        if(!next_buffer.empty()){
            for(const auto& log_line:next_buffer){
                file<<log_line<<"\n";
            }
            file.flush();
            next_buffer.clear();
        }
        
            
         }
         file.close();
        }
    public:
    AsyncLogger(const AsyncLogger&)= delete;
    AsyncLogger& operator=(const AsyncLogger&)= delete;
    static AsyncLogger& getInstance(){
        static AsyncLogger instance;
        return instance;}
    ~AsyncLogger(){
        running=false;
        cv.notify_one();
        if(log_thread.joinable()){
            log_thread.join();
        }
    }
    void append(const std::string& msg){
        std::lock_guard<std::mutex> lock(mtx);
        current_buffer.push_back(msg);
        if(current_buffer.size()>100){
            cv.notify_one();
        }
    }
    



};
inline std::string getCurrentTimeStr(){
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    return ss.str();

}
#define LOG_BASE(level,format,...)\
do{\
    char loc_buffer[1024];\
    snprintf(loc_buffer,sizeof(loc_buffer),"[%s] [%s] " format ,getCurrentTimeStr().c_str(),level,##__VA_ARGS__);\
    AsyncLogger::getInstance().append(std::string(loc_buffer));\
}while(0)
#define LOG_INFO(format,...) LOG_BASE("INFO",format,##__VA_ARGS__)
#define LOG_ERROR(format,...) LOG_BASE("ERROR",format,##__VA_ARGS__)

