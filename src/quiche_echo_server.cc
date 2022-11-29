#include <seastar/core/app-template.hh>
#include <seastar/util/log.hh>
#include <iostream>
#include <stdexcept>
#include <seastar/core/distributed.hh>
#include <seastar/core/sleep.hh>
#include "seastar/net/api.hh"
#include <server/server.h>

seastar::future<> submit_to_cores(std::uint16_t port, std::string &cert, std::string &key) {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
          [port, &cert, &key](unsigned core) {
              return seastar::smp::submit_to(core, [port, &cert, &key] {
                  Server server(port);
                  server.server_setup_config(cert, key);
                  return seastar::do_with(std::move(server), [] (Server &server) {
                      return server.service_loop();
                  });
              });
          });
}

int main(int argc, char **argv) {
    seastar::app_template app;

    namespace po = boost::program_options;
    app.add_options()
            ("port", po::value<std::uint16_t>()->default_value(1234), "listen port")
            ("cert", po::value<std::string>()->default_value("./cert.crt"), "certificate")
            ("key", po::value<std::string>()->default_value("./cert.key"), "key");

    try {
        app.run(argc, argv, [&] () {
            auto &&config = app.configuration();
            std::uint16_t port = config["port"].as<std::uint16_t>();
            std::string cert = config["cert"].as<std::string>();
            std::string key = config["key"].as<std::string>();

            return seastar::do_with(port, std::move(cert), std::move(key),
                                    [] (auto &port, auto &cert, auto &key) {
               return submit_to_cores(port, cert, key);
            });
        });
    } catch (...) {
        std::cerr << "Couldn't start application: "
                  << std::current_exception() << "\n";
        return 1;
    }
    return 0;
}
