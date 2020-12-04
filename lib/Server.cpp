/*
 * Author:      tkoniy
 * Date         2020/11/24
 * Discription: 服务器类的实现
 * LastModify:
 *  修复popNextJSON中可能存在的隐患
 */

#include "Server.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <cerrno>
#include <sstream>
#include <fcntl.h>

namespace nlm = nlohmann;
using namespace tkoniy;

//init static
Server *Server::_instance = nullptr;
std::mutex Server::_singleton_mutex;


int Server::check(int ret,
                  const std::string &errmsg = "Error",
                  const std::function<void()> &catcher = nullptr) {
    if (ret == ERROR) {
        std::stringstream ss(errmsg);
        ss << std::strerror(errno);         //添加系统保存的错误信息
        std::cerr << ss.str() << std::endl; //输出到stderr
        logStr(ss.str());                   //输出到日志
        // if (errno == 9) throw 9;
        if (catcher) catcher();
        return ERROR;
    }
    return SUCCESS;
}

Server &Server::app() {
    // 单例模式
    if (_instance == nullptr) {
        std::unique_lock<std::mutex> lck(_singleton_mutex);
        if (_instance == nullptr) { _instance = new Server; }
    }
    return *_instance;
}

[[noreturn]] void Server::run(uint16_t port, const char *ip) {
    /*socket()*/
    check(_server_fd = socket(AF_INET, SOCK_STREAM, 0),
          "socket() error", []() { exit(-1); });

    /*bind()*/
    sockaddr_in server_addr{};
    inet_pton(AF_INET, ip, &server_addr.sin_addr.s_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    check(bind(_server_fd, (sockaddr *) &server_addr, sizeof(server_addr)),
          "bind() error", []() { exit(-1); });

    /*listen*/
    check(listen(_server_fd, N_LISTEN),
          "listen() error", []() { exit(-1); });

    /*set nonblock*/
    check(setNonBlockFd(_server_fd),
          "setNonBlocking error", []() { exit(-1); });

    /*init epoll*/
    check(_epoll_fd = epoll_create(SIZE_EPOLL_CREATE),
          "epoll_create() error", []() { exit(-1); });

    addServerFd();

    emit("server-ok", {}, _server_fd);

    /*
     * epoll loop
     * epoll_wait的最长等待时间为10ms
     * 当前架构里面, 只需要监听EPOLLIN事件
     * 处理流程如下:
     *  监听到fd可读 -> 调用readET()将fd的新内容读入它的缓存
     *              ->  从缓存区中取出一个报文 -> 解析报文得到事件名称
     *              ->  发送该事件对应的信号, 并将报文内容传递给事件处理器
     */
    auto events = new epoll_event[MAX_EVENTS];
    while (true) {
        int nfds;
        check(nfds = epoll_wait(_epoll_fd, events, MAX_EVENTS, 10),
              "epoll_wait() error", []() { exit(-1); });

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == _server_fd) {
                acceptConnection(); //主线程accept
            } else {
                int client_fd = events[i].data.fd;

                if (events[i].events & EPOLLOUT) { /*暂时用不上这一段代码*/
                    int n_written = writeET(_outbuffer[client_fd].c_str(),
                                            _outbuffer[client_fd].length(),
                                            client_fd);
                    deleteBuffer(client_fd, n_written, IO::OUT);//删掉已发送部分
                }

                if (events[i].events & EPOLLIN) {
                    logStr("start reading");
                    check(readET(client_fd), "", [client_fd, this]() {
                        delClientFd(client_fd);
                    });                             //读取报文到缓存
                    logStr("reading finish");

                    while (true) {                  //分派已经发生的事件
                        nlm::json data = popNextJSON(client_fd);

                        //在这一层过滤掉错误事件和处理buffer,
                        if (data["event"] == "error-fd") {
                            delClientFd(client_fd);
                            break;
                        }
                        if (data["event"] == "not-enough-buffer"
                            || data["event"] == "clean") {
                            break;
                        }
                        emit(data["event"], data, client_fd);
                    }

                } else {                            //出错了
                    std::stringstream msg;
                    msg << "event error, fd=" << events[i].data.fd;
                    logStr(std::stringstream().str());
                    delClientFd(client_fd);         //删除fd
                }
            }
        }

        //处理通过runInLoop挂在IO线程的等待队列上的任务。
        _task_mutex.lock();
        while (!_tasks.empty()) {
            auto task = std::move(_tasks.front());
            _tasks.pop();
            task();
        }
        _task_mutex.unlock();
    }
}

int Server::on(const std::string &ev,
               const std::function<void(const nlm::json &, int)> &callback) {
    _reg_map.insert({ev, callback});
    return SUCCESS;
}

int Server::emit(const std::string &ev, const nlm::json &data, int fd) {
    auto got = _reg_map.find(ev);               //查找事件处理器
    if (got == _reg_map.end()) return ERROR;    //不存在事件,错误
    auto func = got->second;
    _thread_pool->add(func, data, fd);          //将事件处理器分派到线程池执行
    return SUCCESS;
}

int Server::addServerFd() {
    /*epoll add _server_fd*/
    epoll_event ev{};
    ev.data.fd = _server_fd;
    ev.events = EPOLLIN; /* LT mode */
    return check(epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _server_fd, &ev),
                 "epoll add()serverfd error", []() { exit(-1); });
}

int Server::modClientFd(int fd, int ev_t) {
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = ev_t;
    return check(epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev),
                 "modClientFd(1) error");
}

int Server::modClientFd(int fd, int ev_t, void *ptr) {
    epoll_event ev{};
    ev.data.ptr = ptr;
    ev.events = ev_t;
    return check(epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev),
                 "epoll mod() error");
}

int Server::delClientFd(int fd) {
    check(epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, nullptr),
          "epoll mod() delete clientfd error");
    if (_fd_set.find(fd) != _fd_set.end()) {    //防止二次close一个socket
        std::stringstream ss;
        ss << "fd:" << fd << " closed, buffer ereased";
        logStr(ss.str());
        close(fd);
        _inbuffer.erase(fd);
        _outbuffer.erase(fd);
    }
    return emit("fd-out", {}, fd); //emit fd-out event
}

int Server::addClientFd(int fd, int ev_t) {
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = ev_t;

    _fd_set.insert(fd);

    return check(epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev),
                 "epoll add() error");
}

// int Server::handleEpollIN(epoll_data_t ev_data) {
// int n_recv; //接收到的長度
// int client_fd = ev_data.fd;
// //接收首部
// uint32_t length = 0; //报文长度
// check(n_recv = recv(client_fd, &length, sizeof(uint32_t), 0), "recv() header error");
// if (n_recv == 0) {
//     delClientFd(client_fd);
//     return ERROR;
// }
// // std::this_thread::sleep_for(std::chrono::milliseconds(100));
// //接收报文并存储到string中
// std::string data; //存储报文内容
// char buffer[BUF_SIZE]{}; //缓冲区
// while (length > 0) {
//     check(n_recv = recv(client_fd, buffer, length, 0), "recv () msg error");
//     if (n_recv == 0) {
//         delClientFd(client_fd);
//         return ERROR;
//     } //关闭连接
//     data.append(buffer, buffer + n_recv);
//     length -= n_recv;
// }

// modClientFd(client_fd, EPOLLIN | EPOLLONESHOT); //默认监听可写事件
// 这不对, 因为设置了就可能立刻别的线程就来了

/*输出日志*/
//     std::stringstream record;
//     record << "recv data from: " << client_fd << ": " << data;
//     logStr(record.str());
//     /*构造数据并发送事件信号(同步的)*/
//     nlm::json json_data = nlm::json::parse(data);
//     emit(json_data["event"], json_data, client_fd);
//     return 0;
// }

// int Server::handleEpollOUT(epoll_data_t ev_data) {
//     /*send()*/
//     auto epoll_data = static_cast<EpollData *>(ev_data.ptr);
//     int client_fd = epoll_data->fd;
//     std::string data = epoll_data->json_data.dump();
//     delete epoll_data; //把上面的删除
//
//     /*construct msg*/
//     uint32_t length = data.length();
//     check(send(client_fd, &length, sizeof(uint32_t), 0), "send() error");
//     check(send(client_fd, data.c_str(), length, 0), "send() error");
//
//     /* 输出日志*/
//     std::stringstream record;
//     record << "send data to: " << client_fd << ": " << data;
//     logStr(record.str());
//
//     modClientFd(client_fd, EPOLLIN | EPOLLONESHOT); //会将fd域重新置为clientfd
//
//     return 0;
// }

int Server::acceptConnection() {
    /*accept*/
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd;
    if (ERROR ==
        check(client_fd = accept(_server_fd, (sockaddr *) &client_addr, &addr_len))) {
        return ERROR;
    }

    /*set nonblock*/
    std::stringstream ss;
    ss << "set client fd nonblocking error." << client_fd;
    if (ERROR ==
        check(setNonBlockFd(client_fd), ss.str(), [client_fd]() { close(client_fd); })) {
        return ERROR;
    }

    /* add fd*/
    std::stringstream ss1;
    ss1 << "接收新连接 fd = " << client_fd;
    logStr(ss1.str());
    int ret = check(addClientFd(client_fd, EPOLLIN | EPOLLET),
                    "add clientfd failed-> close it",
                    [client_fd]() { close(client_fd); });

    /*发送新连接添加信号*/
    if (ret == SUCCESS) emit("fd-in", {}, client_fd); //emit event
    return ret;

}

int Server::syncSend(const nlm::json &data, int fd) {
    /*制作报文*/
    std::string data_str = data.dump();
    uint32_t h_len = data_str.length();
    uint32_t n_len = htonl(h_len);
    std::string header_str((char *) &n_len, sizeof(uint32_t));
    header_str += data_str;

    /*挂一个writeET任务*/
    auto fu = runInLoop([this, header_str, fd]() {
        return writeET(header_str.c_str(), header_str.length(), fd);
    });

    /*日志*/
    std::stringstream ss;
    ss << "syncSend to fd=" << fd << " called: " << data_str;
    logStr(ss.str());

    /*阻塞等待获取返回值*/
    int ret = fu.get();

    /*日志*/
    std::stringstream ss1;
    logStr(ss1.str());

    return ret;
}

std::future<int> Server::asyncSend(const nlm::json &data, int fd) {
    /*制作报文*/
    std::string data_str = data.dump();
    uint32_t h_len = data_str.length();
    uint32_t n_len = htonl(h_len);
    std::string header_str((char *) &n_len, sizeof(uint32_t));
    header_str += data_str;

    /*挂一个writeET任务*/
    auto fu = runInLoop([this, header_str, fd]() {
        return writeET(header_str.c_str(), header_str.length(), fd);
    });

    /*日志*/
    std::stringstream ss;
    ss << "asyncSend to fd=" << fd << "called: " << data_str;
    logStr(ss.str());

    return fu;
}

void Server::asyncSend(const nlohmann::json &data, int fd, const std::function<void(int)> &callback) {
    /*制作报文*/
    std::string data_str = data.dump();
    uint32_t h_len = data_str.length();
    uint32_t n_len = htonl(h_len);
    std::string header_str((char *) &n_len, sizeof(uint32_t));
    header_str += data_str;

    /*挂一个writeET任务*/
    auto fu = runInLoop([this, header_str, fd, callback]() {
        callback(writeET(header_str.c_str(), header_str.length(), fd));
    });

    /*日志*/
    std::stringstream ss;
    ss << "asyncSend to fd=" << fd << "called: " << data_str;
    logStr(ss.str());

}


int Server::enableFileLog(const std::string &file_path, bool overwrite) {
    return _logger.setFile(file_path, overwrite);
}

int Server::logStr(const std::string &msg) {
    return _logger.log(msg);
}

int Server::resetWorkersNum(size_t n) {
    delete _thread_pool;
    _thread_pool = new ThreadPool(n);
    return SUCCESS;
}

int Server::appendBuffer(int fd, const std::string &buf, IO in_out) {
    auto &buffer_map =
            (in_out == IO::IN ? _inbuffer : _outbuffer);
    auto got = buffer_map.find(fd);
    if (got == buffer_map.end()) {
        buffer_map[fd] = buf; //添加映射
        return SUCCESS;
    }
    buffer_map[fd].append(buf); //追加缓存
    return SUCCESS;
}

int Server::deleteBuffer(int fd, int n, Server::IO in_out) {
    auto &buffer_map =
            (in_out == IO::IN ? _inbuffer : _outbuffer);
    auto got = buffer_map.find(fd);
    if (got == buffer_map.end()) {
        return ERROR;
    }
    auto &str = buffer_map[fd];
    if (str.size() < n) {
        return ERROR;
    }
    str.assign(str, n, str.size() - n); /*截取字符串*/

    return SUCCESS;
}

int Server::writeET(const char *buf, int n, int fd) {
    int n_written = 0;  /*成功写入的字节数*/
    while (n > 0) {     /*剩字节数*/
        int n_write = write(fd, buf, n);
        if (n_write < n) {
            if (n_write == -1) {
                if (errno != EAGAIN) {
                    std::stringstream ss;
                    ss << "error writting fd=" << fd
                       << " expected=" << n
                       << " finally=" << n_written;
                    logStr(ss.str());
                }
                return n_written;
            }
        }
        n -= n_write;
        n_written += n_write;
    }
    return n_written;
}

int Server::readET(int fd) {
    char buf[BUF_SIZE];
    while (true) {
        int n_recv = read(fd, buf, BUF_SIZE);
        if (n_recv == -1) {
            if (errno == EAGAIN)return SUCCESS;
            else if (errno == EINTR) continue;
            else return ERROR;
        } else if (n_recv == 0) { //closed
            return ERROR;
        }
        _inbuffer[fd].append(buf, n_recv); // add to buffer
    }
    // return SUCCESS;
}

int Server::setNonBlockFd(int fd) {
    int oldSocketFlag = fcntl(fd, F_GETFL, 0);
    int newSocketFlag = oldSocketFlag | O_NONBLOCK;
    return fcntl(fd, F_SETFL, newSocketFlag);
}

nlm::json Server::popNextJSON(int fd) {
    //从缓存中取出一个完整报文
    //todo限制最大长度,添加计时器
    if (_inbuffer.find(fd) == _inbuffer.end())
        return {{"event", "clean"}}; //如果fd已被删除,或fd还没有buffer项, 则返回clean

    auto &str = _inbuffer[fd];
    auto p = str.c_str();
    uint32_t n_len = *((uint32_t *) p); //网络序的len
    uint32_t h_len = ntohl(n_len);      //主机序的len

    std::stringstream ss;
    ss << "popNextJSON() for fd:" << fd;

    if (h_len > str.size()) {           //没有足够长度的可以解析
        ss << " failed, expected " << h_len
           << " bytes, but got " << str.size()
           << " bytes in buffer.";
        logStr(ss.str());
        return {{"event", "not-enough-buffer"}};
    }
    if (h_len < 0) {                    //头部的长度字段不合法
        ss << "len < 0 -> delete fd";
        logStr(ss.str());
        return {{"event", "error-fd"}}; //返回错误事件
    }
    if (str.empty() || h_len == 0) {    //长度为0
        ss << "len = 0 -> _inbuffer is cleared";
        logStr(ss.str());
        return {{"event", "clean"}};
    }
    nlm::json data = nlm::json::parse(
            str.begin() + sizeof(uint32_t),
            str.begin() + sizeof(uint32_t) + h_len);

    if (!data.contains("event")) { //不能用try-catch, 里面是assert
        ss << " json data not legal -> delete fd";
        logStr(ss.str());
        return {{"event", "error-fd"}};
    }

    //正确的情况
    deleteBuffer(fd, sizeof(uint32_t) + h_len, IO::IN);
    ss << " resolve a package: " << data;
    logStr(ss.str());
    return data;
}