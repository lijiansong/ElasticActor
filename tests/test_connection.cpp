#include <iostream>
#include <thread>

#include "mola/multiplexer.hpp"
#include "mola/connection.hpp"

using namespace mola;

class Echo : public StreamManager {
public:
  Echo(BaseClient *client)
    : StreamManager(client, 6) {}

  void io_failure(Operation op) override {
    std::cerr << ">>>> IO error when " 
             << (op == Operation::READ ? "reading" : "writing")
             << std::endl;
  }

  void consume(StreamBuffer *buf) {
    std::cout << ">>>> Got a string : "
              << buf->data() << std::endl;
    delete buf;
  }

};

int main() {
  std::string str;
  /* init the epoll backend */
  Multiplexer::initialize();

  /* establish the tcp server */
  TCPServer<TCPClient<Echo> > echo_server("*", 65344);

  /* listening ..*/
  echo_server.start();

  /* create a client to connect this server */
  TCPClient<Echo> client;

  auto ret = client.connect("localhost", 65344);
  std::cout << "connect return " << ret << std::endl;

  while (std::getline(std::cin, str)) {
    if (str.length() == 0) continue;
    std::cout << str << std::endl;

    client.async_write(str.c_str(), str.length());
  }
  
  client.stop();
  echo_server.stop();

  return 0;
}

