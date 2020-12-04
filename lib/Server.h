/*
 * Author:      tkoniy
 * Date         2020/11/24
 * Discription: 服务器类,单例模式
 * Notice:
 *  Server类的使用必须按以下顺序,否则后果自负:
 *      1. 注册事件:    app().on();
 *      2. 启动服务器:   app().start();
 * LastModefy:
 *      添加_fd_set及访问它的锁, 用于管理连接。
 */

#ifndef TODOSERVER_SERVER_H
#define TODOSERVER_SERVER_H

#include <sys/epoll.h>

#include <mutex>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <json.hpp>
#include <utility>
#include <fstream>
#include "Logger.h"
#include "ThreadPool.h"

namespace tkoniy {

    class Server {
    private:
        // struct EpollData {
        //     EpollData(int fd_, nlohmann::json data_) :
        //             fd(fd_), json_data(std::move(data_)) {}
        //
        //     int fd;
        //     nlohmann::json json_data;
        // };

        enum IO {
            IN,
            OUT
        };

        static Server *_instance;                   //单例模式对象
        static std::mutex _singleton_mutex;         //单例模式锁
        static const int N_LISTEN = 5;              //listen()的参数
        static const int SIZE_EPOLL_CREATE = 1024;  //epoll_create()的参数
        static const int MAX_EVENTS = 5000;         //最大监听的事件
        static const int BUF_SIZE = 1024;           //接收和发送数据时, 缓冲区的大小


        int _server_fd = -1;
        int _epoll_fd = -1;

        std::unordered_map<
                std::string,
                std::function<void(const nlohmann::json &data, int fd)>
        > _reg_map;                                 //注册的事件表
        Logger _logger;                             //日志输出器
        ThreadPool *_thread_pool = new ThreadPool;  //线程池

        //除了任务队列之外的成员都不用加锁, 因为不允许在其他线程执行
        std::unordered_set<int> _fd_set;                //维护所有的fd
        std::unordered_map<int, std::string> _inbuffer; //应用层接收缓存
        std::unordered_map<int, std::string> _outbuffer;//应用层发送缓存, 给异步发送消息用的
        std::queue<std::function<void()>> _tasks;       //希望在IO线程执行的任务,和线程池的一样
        std::mutex _task_mutex;                         //锁住任务队列

        /*将fd设置为非阻塞*/
        static int setNonBlockFd(int fd);

        /*对epoll_ctl接口的一些简单封装*/

        int addServerFd();

        int addClientFd(int fd, int ev_t);

        int modClientFd(int fd, int ev_t);              //data.fd = fd

        int modClientFd(int fd, int ev_t, void *ptr);   //data.ptr = ptr

        int delClientFd(int fd);

        /*
         * Params:      fd          描述符
         *              buf         要添加的缓存
         *              in_out      指定添加对象(接收/发送缓存)
         * Return:      成功/失败
         */
        int appendBuffer(int fd, const std::string &buf, IO in_out);

        /*
         * Params:      fd          描述符
         *              n           要删除多少缓存
         *              in_out      指定删除对象(接收缓存/发送缓存)
         * Return:      成功/失败
         */
        int deleteBuffer(int fd, int n, IO in_out);//todo

        Server() = default;

        /*
         * 从_inbuffer中弹出下一个json数据
         * 两种错误event: "error-fd", "not-enough-buf"
         * params:      fd          描述符号
         * return:      json        数据
         */
        nlohmann::json popNextJSON(int fd);

        // /*
        //  * Function:    handleEpollIN
        //  * Description: 处理可读事件
        //  * return:      成功/失败
        //  */
        // int handleEpollIN(epoll_data_t ev_data);

        /*
         * Function:    handleEpollOUT
         * Description: 处理可写事件
         * return:      成功/失败
         */
        // int handleEpollOUT(epoll_data_t ev_data);

        /*
         * Function:    AcceptConnection
         * Description: 接收新连接
         * Return:      fd
         */
        int acceptConnection();

        /*
         * Function:    writeET
         * Description: 在ET模式下写入完整的信息。
         * Params:      buf         要write的缓存
         *              n           要write的长度
         *              fd          要向哪个socket连接write
         * Return:      成功写入的字节数
         */
        int writeET(const char *buf, int n, int fd);

        /*
         * Function:    readRT
         * Description: 在ET模式下将数据读入缓存中,直到读到EAGAIN
         * Params:      fd          描述符
         * Return:      字节数/失败
         */
        int readET(int fd);

        /*
         * Funtion:     runInloop
         * Description: 将任务挂在IO线程上执行
         *              传入的函数执行的时候会阻塞eventloop
         * Params:      任意的函数及其参数
         * Return:      函数的future
         */
        template<class F, class...Args>
        auto runInLoop(const F &func, const Args &... args) -> std::future<decltype(func(args...))>;


    public:
        virtual ~Server() = default;

        static const int ERROR = -1;
        static const int SUCCESS = 0;

        /*
         * Function:    check()
         * Description: 错误情况执行回调函数,打印错误消息
         * Params:      ret         要检查的返回值
         *              cather      错误处理函数
         *              errmsg      输出
         * Return:      成功/失败
         */
        int check(int ret, const std::string &errmsg, const std::function<void()> &catcher);

        /*
         * Function:    app()
         * return:      Server&     服务器实例的引用
         * Description: 单例接口,获取服务器实例
         */
        static Server &app();

        /*
         * Function:    on
         * Description: 支持void(json, fd)类型的回调函数
         *              注册一个事件
         * Params:      ev          事件名字
         *              callback    回调函数
         */
        int on(const std::string &ev, const std::function<void(const nlohmann::json &data, int fd)> &callback);

        /*
         * Function:    on
         * Description: 发送一个信号/事件, 触发对应的事件处理器(回调), 将其加入线程池执行
         * Params:      ev          事件名字
         *              data        数据
         *              fd          关联该事件的 fd
         */
        int emit(const std::string &ev, const nlohmann::json &data, int fd);

        /*
         * Function:    run
         * Description: 启动服务器, 请在调用该函数前做好一切准备, 该函数将阻塞整个线程。
         * Params:      port        端口号
         *              ip          ip地址
         */
        [[noreturn]] void run(uint16_t port, const char *ip);

        /*
         * Function:    syncSend
         * Description: 同步发送消息的接口
         *              调用runInLoop执行
         * Params:      fd      socket描述符
         *              data    包的内容
         * Return:      成功
         */
        int syncSend(const nlohmann::json &data, int fd);

        /*
         * Function:    asyncSend
         * Description: 异步发送消息的接口
         *              调用runInLoop执行
         *              和同步的区别仅在于不执行future.get()
         * params:      data        数据
         *              fd          连接
         * Return:      future      内容是成功写入的字节数
         */
        std::future<int> asyncSend(const nlohmann::json &data, int fd);

        /*
         * Function:    asyncSend
         * Description: 异步发送消息的接口(回调函数版本)
         * params:      data        数据
         *              fd          目标连接
         *              callback    回调函数, 参数为成功写入的字节数
         * Return:      void
         */
        void asyncSend(const nlohmann::json &data, int fd, const std::function<void(int)> &callback);

        /*
         * Function:    enableFileLog
         * Description: 启动日志文件
         * Params:      file_path 文件路径
         *              overwrite 是否覆盖
         * return:      成功/失败
         */
        int enableFileLog(const std::string &file_path, bool overwrite = true);

        /*
         * Function:    resetWorkersNum
         * Description: 设置线程池的大小
         * Params:      n   线程数
         */
        int resetWorkersNum(size_t n);

        /*
         * Function:    logStr
         * Description: 输出消息
         * Params:      msg     消息
         * return:      成功
         */
        int logStr(const std::string &msg);

    };

    template<class F, class...Args>
    auto Server::runInLoop(const F &func, const Args &... args) -> std::future<decltype(func(args...))> {
        /*
         * 对线程池代码稍作修改使用
         * todo 没有添加关闭状态
         */
        typedef decltype(func(args...)) RetType;
        auto f = std::bind(func, args...);
        auto ptr_task = new std::packaged_task<RetType()>(f);
        std::future<RetType> fu = ptr_task->get_future();

        // std::unique_lock<std::mutex> lock(_task_mutex);
        _task_mutex.lock();
        _tasks.push([ptr_task]() {
            (*ptr_task)();
            delete ptr_task;
        });
        // lock.unlock();
        _task_mutex.unlock();
        return fu;
    }
}

#endif //TODOSERVER_SERVER_H
