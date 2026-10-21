# WebServer Baseline Record

- **开发与运行环境**: VMware 虚拟机 
- **构建工具**: CMake / Make (请补充你实际使用的完整编译指令，例如 `make` 或 `cmake --build build`)
- **启动命令**: ./webserver
- **静态资源目录**: (待补充，例如 `/home/jiashuai/webserver/resources`)
- **核心架构快照**:
  - 事件驱动: Epoll (ET 边缘触发模式) + 非阻塞 Socket
  - 内存与文件: Mmap 内存映射技术
  - HTTP 处理: 基于有限状态机 (FSM) 的报文解析
  - 连接保活: 最小堆 (Min-Heap) 定时器
  - 性能基建: 线程池 + 双缓冲异步日志 (Double-Buffering Asynchronous Logging)
- **数据库状态**:
  - 引擎: MySQL / MariaDB
  - 库名: (待补充)
  - 表结构: (待执行 `DESC user;` 后补充字段细节，确认除 username 和 password 外是否有其他列)