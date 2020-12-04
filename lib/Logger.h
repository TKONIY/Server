#ifndef TODOSERVER_LOGGER_H
#define TODOSERVER_LOGGER_H
/*
 * Author:      tkoniy
 * Date         2020/11/27
 * Discription: Logger类
 * Notice:      单独拿出来写是为了保证线程安全。
 */
#include <iostream>
#include <fstream>
#include <mutex>

namespace tkoniy {

    class Logger {
    private:
        std::fstream _file;
        std::mutex _mutex;
    public:
        static const int SUCCESS = 0;
        static const int ERROR = -1;

        Logger() = default;;

        virtual ~Logger() = default;;

        int log(const std::string &msg) {

            _mutex.lock();
            //获取时间
            auto clock = std::chrono::system_clock::now();
            auto currtime = std::chrono::system_clock::to_time_t(clock);
            //输出到文件&stdout
            std::string datatime(std::ctime(&currtime));
            datatime.pop_back(); //删除末尾的\n符号
            if (_file.is_open()) _file << datatime << "    " << msg << std::endl;
            std::cout << datatime << "    "  << msg << std::endl;
            _mutex.unlock();

            return SUCCESS;
        }

        int setFile(const std::string &file_path, bool overwrite) {

            _mutex.lock(); //lock
            if (_file.is_open()) _file.close();
            auto mode = std::ios::out;
            mode |= overwrite ? std::ios::trunc : std::ios::app;
            _file.open(file_path, mode);
            int ret = _file.is_open() ? SUCCESS : ERROR;
            _mutex.unlock(); // unlock

            return ret;
        }
    };

}

#endif //TODOSERVER_LOGGER_H
