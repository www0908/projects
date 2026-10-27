#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>
#include <vector>
#include <stdexcept>
#include <iostream>

// 状态枚举：精确反馈任务拒绝原因，替代单薄的 bool
enum class EnqueueResult {
    SUCCESS,
    QUEUE_FULL,    // 队列满：外层网络层应执行限流、断开连接并记录限频日志
    POOL_CLOSED    // 池已关：服务器正在停机，外层直接丢弃请求即可
};

class ThreadPool {
private:
    std::mutex mtx_;
    std::condition_variable cond_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    size_t maxQueueSize_;
    bool isClosed_; // 统一用 isClosed_，不用再搞一个冗余的 stop 变量

public:
    // 构造函数：参数名修正，加入容量校验和异常保护
    explicit ThreadPool(size_t threadCount = 8, size_t maxQueueSize = 10000)
        : maxQueueSize_(maxQueueSize), isClosed_(false) {
        
        // 致命参数拦截
        if (threadCount == 0 || maxQueueSize == 0) {
            throw std::invalid_argument("线程池参数 threadCount 和 maxQueueSize 必须大于 0");
        }

        workers_.reserve(threadCount);

        try {
            for (size_t i = 0; i < threadCount; ++i) {
                workers_.emplace_back([this]() {
                    // 工作线程的死循环状态机
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->mtx_);
                            // 挂起等待：直到“线程池关闭”或者“队列里有任务”才醒来
                            this->cond_.wait(lock, [this]() {
                                return this->isClosed_ || !this->tasks_.empty();
                            });

                            // 平滑退出逻辑：如果关门了，且候车室空了，就可以下班了。
                            // 注意：如果关门了但队列里还有任务，这里不会 break，而是继续往下走去清库存！
                            if (this->isClosed_ && this->tasks_.empty()) {
                                break;
                            }

                            // 取出任务
                            task = std::move(this->tasks_.front());
                            this->tasks_.pop();
                        } // 局部锁在此刻释放

                        // 异常隔离装甲：防止单个糟糕的业务请求崩溃把整个工作线程带走
                        try {
                            task();
                        } catch (const std::exception& e) {
                            // 实际项目中把这里换成你的 AsyncLogger 记录
                            std::cerr << "[ThreadPool Error] 任务执行异常: " << e.what() << '\n';
                        } catch (...) {
                            std::cerr << "[ThreadPool Error] 任务执行抛出未知异常\n";
                        }
                    }
                });
            }
        } catch (...) {
            // 兜底防御：如果创建第 N 个线程时系统资源爆了，必须把前面已经建好的 N-1 个线程优雅关掉
            {
                std::lock_guard<std::mutex> lock(mtx_);
                isClosed_ = true;
            }
            cond_.notify_all();
            for (auto& worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            throw; // 清理完现场后，把异常扔给主进程
        }
    }

    // 析构函数：优雅停机，保证所有在途任务全部跑完再销毁
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            isClosed_ = true; // 1. 关门，拒收新客
        }
        cond_.notify_all(); // 2. 拿个大喇叭把所有睡觉的空闲线程叫醒去清库存
        
        // 3. 等待所有工作线程执行完剩余任务并体面退出
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // 提交任务：带容量拦截和状态校验
    template<class F>
    EnqueueResult AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            
            if (isClosed_) {
                return EnqueueResult::POOL_CLOSED;
            }
            
            if (tasks_.size() >= maxQueueSize_) {
                return EnqueueResult::QUEUE_FULL; // 触达容量上限，直接拒收并向外围报警
            }
            
            tasks_.emplace(std::forward<F>(task));
        }
        // 解锁后再通知：防止线程刚醒来就撞上没释放的锁，白白浪费一次 CPU 上下文切换
        cond_.notify_one();
        return EnqueueResult::SUCCESS;
    }

    // 禁用拷贝和赋值，线程池这种重量级资源绝对不允许被复制
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};

#endif // THREADPOOL_H