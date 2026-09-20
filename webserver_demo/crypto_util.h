#ifndef CRYPTO_UTIL_H
#define CRYPTO_UTIL_H
#include <string>
#include<openssl/sha.h>
#include<sstream>
#include<iomanip>
inline std::string sha256(const std::string& input){
    const std::string salt="webserver_salt_#2026@!";
    std::string salted_input = salt + input;
    unsigned char hash[SHA256_DIGEST_LENGTH];
   SHA256(reinterpret_cast<const unsigned char*>(salted_input.c_str()), 
           salted_input.length(), 
           hash);
    std::ostringstream oss;
    for(int i=0;i<SHA256_DIGEST_LENGTH;i++){
        oss<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<int>(hash[i]);

    }
    return oss.str();
}
#endif