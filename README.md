# Simple TCP Server
> version 0.1
## Introduction
* Using reactor-model. One event loop thread(I/O thread), and a pool for workers threads.
* Based on event model.  You can register an event handler like node.js.
* Handlers are somehow asynchronized. They will run in other thread independent with I/O thread.
* Use a simple protocol based on TCP.
  ```c
  + - - - - + - - - - - - - + 
  |   len   | json sequence |
  + - - - - + - - - - - - - +
  uint32_t        len; 		 
  json            sequence;	 
  ```
* A simple thread pool based on `std::future`, partially referenced to [progschj](https://github.com/progschj/ThreadPool).
* A simple but thread-safe logger.
## Dependency         
* C++14+ support.
* [nlohmann/json](https://github.com/nlohmann/json) 
## Server Examples
Include our `Server.h` and Nlohmann's `json.hpp`.
```c
#include<Server.h>
#include<json.hpp>
```
A simple C/S echo server.
```c
int main(){
    Server::app().on("hello", [](auto data, int fd){
        data["reply"] = "Hi";
        Server::app().syncSend(data, fd);
    });
    Server::app().run(8888, "127.0.0.1");
}
```
Here's the beautiful output by our Logger.

```text
Wed Dec  2 21:48:14 2020    server 0.0.0.0:8888 start successfully
```

> However, controller can only registered by `app().on()` currently. The controller class will be added soon.

Besides normal synchronized `syncSend`. We also provide `asyncSend`. 
1. Promise-Future Version
```c++
void worker() {
    //...
    auto fu = Server::app().asyncSend("data", data);
    //prepare other data
    fu.get();
    Server::app().asyncSend("data", data2);
    //...
}
```
2. Callback Version
```c++
void worker() {
    Server::app().asyncSend("data", data, fd, [](int ret){
        Server::app().logStr("data successfully sended.");
    });
}
```
## Thread pool Examples

```c
ThreadPool t(10);
auto fu = t.add([](int a, int b) { return a + b; }, 1, 2);
std::cout << fu.get() << std::endl;
```

## Logger Examples

```c
auto logger = new Logger;
Logger.setFile("server.log", true);
Logger.log("hello world");
```

The logger has well formatted output with a timestamp.

```text
Wed Dec  2 21:48:14 2020    hello world
```

## Server Integrated Events

|event |description |
|:--: | :--:|
| `server-ok` | Server is about to enter the event loop. |
| `fd-out` | A connection closed.  |
| `fd-in` | A new connection established. |

## Future Goals
* Version 1.0: Wrap `Connection` class. Hide socket discriptor. Implement `Send()` `Close` as `Connection`'s member. 
* Version 2.0: Support different protocols. Like nodejs `net.CreateServer`. Remove our simple protocol. Support multi-level `Logger`
* Version 3.0: Support simple HTTP server. 
* Version 4.0: Support HTTP server like `express.js`.
* Version 5.0: Add `Handler` class for HTTP server.
* Version 6.0: Add client ...

## Details
* Refer to [design.md](doc/design.md)

Contact me if you would like to contribute.

