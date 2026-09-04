#include<iostream>
#include<string>
enum CheckState{
     STATE_REQUEST_LINE=0,
     STATE_HEADER,
     STATE_FINISH
};
enum HttpCode{
     NO_REQUEST =0,
     GET_REQUEST,
     BAD_REQUEST
};
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
int main(){
    HttpParser parser;
    std::cout<<"开始模拟恶劣的网络环境\n"<<std::endl;
    std::string network_buffer ="GET/study/lin";
    std::cout<<"收到碎片1：["<<network_buffer<<"]\n"<<std::endl;
    HttpCode result =parser.parse(network_buffer);
    std::cout<<"解析结果："<<result<< "0代表NO_REQUEST"<<std::endl;
    network_buffer +="ux/epollserver.html HTTP/1.1\r\nHost: 127";
    std::cout<<"收到碎片2：["<<network_buffer<<"]\n"<<std::endl;
    result =parser.parse(network_buffer);
    std::cout<<"解析结果："<<result<< "0代表NO_REQUEST"<<std::endl;
    network_buffer +=".0.0.1\r\n\r\n";
    std::cout<<"收到碎片3：["<<network_buffer<<"]\n"<<std::endl;
    result =parser.parse(network_buffer);
    std::cout<<"解析结果："<<result<< "1代表GET_REQUEST"<<std::endl;
    return 0;
}