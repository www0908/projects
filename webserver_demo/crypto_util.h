#pragma once
#include<string>

namespace Security{
    std::string HashPassword(const std::string& plian_password);
    bool VerifyPassword(const std::string& plain_password, const std::string& db_hash);
    
   
}