#ifndef SEASTAR_QUICHE_ECHO_DEMO_STREAM_H
#define SEASTAR_QUICHE_ECHO_DEMO_STREAM_H


#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/reactor.hh>
#include <fstream>
#include <utility>
#include <vector>

using namespace seastar::net;

class input{
private:
    seastar::temporary_buffer<char> _buf;
    bool _eof = false;
    std::vector<char> send_file_buffer;

private:
    [[nodiscard]] size_t available() const noexcept { return _buf.size(); }
    void reset() noexcept { _buf = {}; }
public:
    input() noexcept = default;
    input(input&&) = default;

    seastar::future<seastar::temporary_buffer<char>> send_buffer_2(){
        std::string input(send_file_buffer.begin(), send_file_buffer.end());

        return seastar::make_ready_future<seastar::temporary_buffer<char>>((input.c_str(), input.size() + 1));
    }

    seastar::future<seastar::temporary_buffer<char>> read_exactly_part(size_t n, seastar::temporary_buffer<char> out, size_t completed) noexcept {
        if (available()) {
            auto now = std::min(n - completed, available());
            std::copy(_buf.get(), _buf.get() + now, out.get_write() + completed);
            _buf.trim_front(now);
            completed += now;
        }
        if (completed == n) {
            return seastar::make_ready_future<seastar::temporary_buffer<char>>(std::move(out));
        }

        // _buf is now empty
        return send_buffer_2().then([this, n, out = std::move(out), completed] (auto buf) mutable {
            if (buf.size() == 0) {
                _eof = true;
                out.trim(completed);
                return make_ready_future<seastar::temporary_buffer<char>>(std::move(out));
            }
            _buf = std::move(buf);
            return this->read_exactly_part(n, std::move(out), completed);
        });
    }

    seastar::future<seastar::temporary_buffer<char>> read_exactly(size_t n) noexcept {
        if (_buf.size() == n) {
            // easy case: steal buffer, return to caller
            return seastar::make_ready_future<seastar::temporary_buffer<char>>(std::move(_buf));
        } else if (_buf.size() > n) {
            // buffer large enough, share it with caller
            auto front = _buf.share(0, n);
            _buf.trim_front(n);
            return seastar::make_ready_future<seastar::temporary_buffer<char>>(std::move(front));
        } else if (_buf.empty()) {
            // buffer is empty: grab one and retry
            return send_buffer_2().then([this, n] (auto buf) mutable {
                if (buf.size() == 0) {
                    _eof = true;
                    return make_ready_future<seastar::temporary_buffer<char>>(std::move(buf));
                }
                _buf = std::move(buf);
                return this->read_exactly(n);
            });
        } else {
            try {
                // buffer too small: start copy/read loop
                seastar::temporary_buffer<char> b(n);
                return read_exactly_part(n, std::move(b), 0);
            } catch (...) {
                return seastar::current_exception_as_future<seastar::temporary_buffer<char>>();
            }
        }
    }

    seastar::future<seastar::temporary_buffer<char>> read() noexcept{
        using tmp_buf = seastar::temporary_buffer<char>;
        if (_eof) {
            return seastar::make_ready_future<tmp_buf>();
        }
        if (_buf.empty()) {
            return send_buffer_2().then([this] (tmp_buf buf) {
                _eof = buf.empty();
                return seastar::make_ready_future<tmp_buf>(std::move(buf));
            });
        } else {
            return seastar::make_ready_future<tmp_buf>(std::move(_buf));
        }
    }

    seastar::future<seastar::temporary_buffer<char>> read_up_to(size_t n) noexcept{
        using tmp_buf = seastar::temporary_buffer<char>;
        if (_buf.empty()) {
            if (_eof) {
                return seastar::make_ready_future<tmp_buf>();
            } else {
                return send_buffer_2().then([this, n] (tmp_buf buf) {
                    _eof = buf.empty();
                    _buf = std::move(buf);
                    return read_up_to(n);
                });
            }
        } else if (_buf.size() <= n) {
            // easy case: steal buffer, return to caller
            return make_ready_future<tmp_buf>(std::move(_buf));
        } else {
            try {
                // buffer is larger than n, so share its head with a caller
                auto front = _buf.share(0, n);
                _buf.trim_front(n);
                return make_ready_future<tmp_buf>(std::move(front));
            } catch (...) {
                return seastar::current_exception_as_future<tmp_buf>();
            }
        }
    }

    seastar::future<> pass_data(std::vector<char> source) {
        send_file_buffer = std::move(source);
        return seastar::make_ready_future<>();
    }

    bool eof() const noexcept { return _eof; }

    std::vector<char> send_buffer() {
        return send_file_buffer;
    }
};

#endif //SEASTAR_QUICHE_ECHO_DEMO_STREAM_H
