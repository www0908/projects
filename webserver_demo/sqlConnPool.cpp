#include "sqlConnPool.h"

SqlConnPool* SqlConnPool::Instance(){
    static SqlConnPool connpool;
    return &connpool;
}
void SqlConnPool::Init(const char*host,int port,const char*user,const char*pwd,const char*dbname,int connsize){
  for(int i=0;i<connsize;i++){
    MYSQL *sql = mysql_init(nullptr);
    if(!sql){
        std::cerr << "[DB Error] mysql_init 失败!" << std::endl;
        continue;
    }
    MYSQL* conn = mysql_real_connect(sql,host,user,pwd,dbname,port,nullptr,0);
    if(!conn){
        std::cerr << "[DB Error] 连接失败: " << mysql_error(sql) << std::endl;
        mysql_close(sql);
        continue;
    }
    connQue_.push(conn);
  }
  MAX_CONN_ = connsize;
  std::cout << "[DB Info] 数据库连接池初始化完毕，可用连接数: " << connQue_.size() << std::endl;
}
MYSQL* SqlConnPool::GetConn(){
    MYSQL *sql=nullptr;
    std::unique_lock<std::mutex> locker(mtx_);
    cond_.wait(locker,[this](){return !connQue_.empty();});
    sql=connQue_.front();
    connQue_.pop();
    return sql;
}
void SqlConnPool::FreeConn(MYSQL* sql){
    if(sql==nullptr){
        return;
    }
    std::unique_lock<std::mutex> locker(mtx_);  
    connQue_.push(sql);
    cond_.notify_one();
}
void SqlConnPool::ClosePool(){
    std::unique_lock<std::mutex> locker(mtx_);
    while(!connQue_.empty()){
        auto item=connQue_.front();
        connQue_.pop();
        mysql_close(item);
    }
}