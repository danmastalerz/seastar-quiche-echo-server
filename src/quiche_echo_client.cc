#include <cinttypes>
#include <seastar/core/seastar.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/distributed.hh>

#include <seastar/core/app-template.hh>
#include <seastar/util/log.hh>
#include <client/client.h>
#include "connected_socket/connected_client_socket.h"

seastar::future<> submit_to_cores(std::uint16_t port, const char *host, const std::string file) {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
                                      [port, host, file](unsigned core) {
                                          return seastar::smp::submit_to(core, [port, host, file, core]{
                                              connected_client_socket socket(host, port, seastar::this_shard_id(), file, core);
                                              return seastar::do_with(std::move(socket), [](connected_client_socket &socket) {
                                                  std::cerr << "Running client loop...\n";
                                                  return socket.connect().then([&socket] {
                                                      //(void) socket.write();

                                                      return socket.loop();
                                                  });
                                              });
                                          });
                                      });
}


int main(int argc, char **argv) {
    seastar::app_template app;

    namespace po = boost::program_options;
    app.add_options()
            ("port", po::value<std::uint16_t>()->default_value(1234), "listen port");
    app.add_options()
            ("file", po::value<std::string>()->default_value("default_file.txt"), "file to send");

    try {
        app.run(argc, argv, [&]() {
            auto &&config = app.configuration();

            const char *host = "127.0.0.1";
            uint16_t port = 1234;
            std::string file = config["file"].as<std::string>();

            return seastar::do_with(port, std::move(host), file,
                                    [](auto &port, auto &host, auto &file) {
                                        return submit_to_cores(port, host, file);
                                    });
        });
    } catch (...) {
        std::cerr << "Couldn't start application: "
                  << std::current_exception() << "\n";
        return 1;
    }
    return 0;
}

