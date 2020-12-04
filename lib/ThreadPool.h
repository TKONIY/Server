/*
 * Author:      tkoniy
 * Date         2020/11/27
 * Discription: 线程池类
 * Notice:
 *  不是单例,可以开无数个线程池,但是建议别开太多
 *  设计的难点:
 *      * emplace_back可以接收一系列参数
 *      * 析构的时候安全地关闭所有线程
 *      * 以后可以修改成std::future
 *      * package_task如果要当引用传递则必须传右值引用
 *  package_task用法小结:
 *  知识点小结:
 *      * std::function 既可以只存函数指针, 也可以带参数一起存放。
 *      * std::bind(f, arg) 将参数绑定到函数上,返回一个新的function对象(没有参数),可以直接执行。
 *      * std::package_task<ret()> 将一个function封装成package_task, 可以直接执行。
 *      * std::package_task<ret(a,b)>的时候task需要指定参数执行.我们已经绑定了,函数就变成了无参的了,所以不需要这么写
 *      * std::future 是package_task给的存放返回值的东西, fu.get()可以等待返回值
 *      * package_task无拷贝构造,只能传指针/引用
 *      * lambda函数是个神奇的编译技巧, 虽然我的函数类型是function<void()>,
 *          但是它可以接收一个捕获了各种类型的变量的lambda函数,该技巧从
 *          https://github.com/progschj/ThreadPool 中学到了
 */

#ifndef TODOSERVER_THREADPOOL_H
#define TODOSERVER_THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <future>
#include <vector>
#include <queue>
#include <functional>

namespace tkoniy {
    class ThreadPool {
    private:
        std::mutex _mutex; /*锁住所有的成员变量*/
        std::condition_variable _cv_worker; /*给worker睡觉用的*/
        std::queue<std::function<void()>> _tasks; /*任务队列*/
        std::vector<std::thread> _workers; /*工作线程*/
        bool _stop; /*标记关闭*/

        /*
         * Function:    loop
         * Description: 工作线程的内容
         */
        void loop();

    public:
        explicit ThreadPool(size_t n_workers = 10);

        ~ThreadPool();

        template<class F, class...Args>
        decltype(auto) add(const F &func, const Args &... args);

        size_t getWorkerNum() const { return _workers.size(); }
    };

    template<class F, class...Args>
    decltype(auto) ThreadPool::add(const F &func, const Args &... args) {
        /*
         * 这个函数太难写了, 不熟悉c++11之上的语法,查阅了许多资料
         * class ...是可变函数模板
         * 通过decltype推导返回的future类型
         * 使用lambda函数简化
         * 多线程难以管理堆内存,使用智能指针
         */
        typedef decltype(func(args...)) RetType;/*返回值类型*/
        auto f = std::bind(func, args...); /*将参数绑定到一个函数对象上*/
        auto ptr_task = std::make_shared<std::packaged_task<RetType()>>(f); /*为该函数和参数生成package_tasl*/
        std::future<RetType> fu = ptr_task->get_future(); /*获取future*/

        /*添加到任务队列*/
        std::unique_lock<std::mutex> lock(_mutex);
        if (!_stop) {
            _tasks.push([ptr_task]() {
                (*ptr_task)();
            });
        }
        _cv_worker.notify_one(); /*唤醒一个worker*/
        lock.unlock();

        return fu; /*返回对应future*/
    }

}
#endif //TODOSERVER_THREADPOOL_H
