#define ASIO_HAS_CO_AWAIT
#include <coroutine>
#include <cstdlib>
#include <iostream>
#include <system_error>

#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>

void basic_producer_consumer() {
    asio::thread_pool pool{4};

    asio::experimental::concurrent_channel<
        void(std::error_code, int)
    >  chan{pool};

    // producer coro
    asio::co_spawn(
        pool,
        [&chan]() -> asio::awaitable<void> {
            for (int i = 1; i <= 100; i++) {
                co_await chan.async_send({}, i, asio::use_awaitable);
            }
            chan.close();
            co_return;
        },
        asio::detached
    );

    // consumer coro
    asio::co_spawn(
        pool,
        [&chan]() -> asio::awaitable<void> {
            std::error_code ec{};
            for (;;) {
                auto value = co_await chan.async_receive(
                    asio::redirect_error(asio::use_awaitable, ec)
                );
                if (ec) {
                    co_return;
                }
                std::cout << value << std::endl;
            }
        },
        asio::detached
    );

    pool.join();
}

int main(int, char*[]) {
    basic_producer_consumer();
    return EXIT_SUCCESS;
}