//
// Created by win10 on 2020/11/27.
//

#include "ThreadPool.h"


using namespace tkoniy;

ThreadPool::ThreadPool(size_t n_workers)
        : _stop(false) {
    for (size_t i = 0; i < n_workers; ++i) {
        // create threads
        _workers.emplace_back(&ThreadPool::loop, this);
    }
}

ThreadPool::~ThreadPool() {
    std::unique_lock<std::mutex> lock(_mutex);
    _stop = true;
    _cv_worker.notify_all(); /*唤醒所有线程*/
    lock.unlock();

    for (auto &worker: _workers) {
        worker.join(); /*等待所有线程结束*/
    }
}

void ThreadPool::loop() {
    while (true) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv_worker.wait(lock, [this]() -> bool {
            return this->_stop || !this->_tasks.empty();
        }); /*等到队列空了/标记了结束就醒来工作*/
        if (_stop && _tasks.empty())return; /*任务清空且收到停止信号,退出线程*/
        auto task = std::move(_tasks.front()); /*移动构造,避免复制参数*/
        _tasks.pop();/*删除任务*/
        lock.unlock(); /*释放锁*/

        task(); /*执行任务*/
    }
}
