#include "../lib/Server.h"
#include <json.hpp>

using namespace tkoniy;
int main() {
    Server::app().on("hello", [](auto data, int fd) {
        data["reply"] = "Hi";
        Server::app().syncSend(data, fd);
    });
    Server::app().run(8888, "127.0.0.1");
}
