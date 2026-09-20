#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>

class SqlConnPool{
    public:
    static SqlConnPool* Instance();
    void Init(const char * host, int port, const char * user, const char * pwd, const char * dbname, int connsize=10);
    MYSQL* GetConn();
    void FreeConn(MYSQL* conn);
    void ClosePool();
    private:
    SqlConnPool()=default;
    ~SqlConnPool(){ClosePool();}
    int MAX_CONN_;
    std::queue<MYSQL*> connQue_;
    std::mutex mtx_;
    std::condition_variable cond_;
};
class SqlConnRAII{
    public:
    SqlConnRAII(MYSQL** sql, SqlConnPool* connpool){
        *sql =connpool->GetConn();
        connpool_=connpool;
        sql_=*sql;


    }
    ~SqlConnRAII(){
        if(sql_){
            connpool_->FreeConn(sql_);
        }
    }
    private:
    MYSQL* sql_;
    SqlConnPool* connpool_;
    
};
#endif