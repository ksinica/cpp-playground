#define ASIO_HAS_CO_AWAIT
#include <chrono>
#include <coroutine>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <system_error>
#include <tuple>
#include <unordered_set>

#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace detail {
asio::awaitable<void> async_timeout(asio::steady_timer::duration duration, 
    std::error_code& ec) {
    asio::steady_timer timer{co_await asio::this_coro::executor};
    timer.expires_after(duration);
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
}

template<typename T, typename E, typename Traits, typename... Sigs>
asio::awaitable<void> async_channel_receive(
    asio::experimental::basic_concurrent_channel<E, Traits, Sigs...>& channel, 
    T& value, 
    std::error_code& ec) {
    value = co_await channel.async_receive(
        asio::redirect_error(asio::use_awaitable, ec)
    );
}

template<typename E, typename Traits, typename... Sigs>
void channel_close(
    asio::experimental::basic_concurrent_channel<E, Traits, Sigs...>& channel) {
    channel.cancel();
    channel.close();
}

template<typename T>
class observable_impl : public std::enable_shared_from_this<observable_impl<T>>{
private:
    struct private_tag {};

    using channel_type = asio::experimental::concurrent_channel<
        void(std::error_code, T)
    >;
public:
    observable_impl(private_tag, asio::io_context& context) 
        : m_context{context} 
    {}

    virtual ~observable_impl() try { close_all(); } catch(...) {}

    static auto create(asio::io_context& context) {
        return std::make_shared<observable_impl<T>>(private_tag{}, context);
    }

    auto subscribe() {
        auto channel = std::make_shared<channel_type>(m_context);
        {
            std::unique_lock lock{m_mutex};
            m_channels.insert(channel);
        }
        return std::make_tuple(
            [channel](auto timeout) -> asio::awaitable<std::optional<T>> {
                using namespace asio::experimental::awaitable_operators;

                if (!channel->is_open()) {
                    co_return std::nullopt;
                }

                T value{};
                std::error_code ec, ec2{};

                co_await (
                    async_channel_receive(*channel, value, ec) ||
                    async_timeout(timeout, ec2)
                );
                
                if (ec || ec2 == asio::error::timed_out) {
                    co_return std::nullopt;
                }
                
                co_return std::move(value);
            },
            [channel, weak_this = observable_impl<T>::weak_from_this()]() {
                if (auto o = weak_this.lock()) {
                    o->unsubscribe(channel);
                }
            }
        );
    }

    void emit(T value) {
        std::shared_lock lock{m_mutex};
        for (const auto& c : m_channels) {
            c->async_send({}, value, asio::detached);
        }
    }

    void close_all() {
        decltype(m_channels) channels{};
        {
            std::unique_lock lock{m_mutex};
            m_channels.swap(channels);
        }
        for (const auto& c : channels) {
            channel_close(*c);
        }
    }

private:
    void unsubscribe(const std::shared_ptr<channel_type>& channel) {
        {
            std::unique_lock lock{m_mutex};
            m_channels.erase(channel);
        }
        channel_close(*channel);
    }

    asio::io_context& m_context;
    std::shared_mutex m_mutex;
    std::unordered_set<std::shared_ptr<channel_type>> m_channels;
};
} // detail

template<typename T>
struct subscription {
    const std::function<
        asio::awaitable<std::optional<T>>(asio::steady_timer::duration)
    > next;
    const std::function<void()> unsubscribe;
};

template<typename T>
class observable {
public:
    explicit observable(asio::io_context& context) 
        : m_impl{detail::observable_impl<T>::create(context)}
    {}

    virtual ~observable() {}

    subscription<T> subscribe() const {
        const auto [next, unsub] = m_impl->subscribe();
        return subscription<T>{std::move(next), std::move(unsub)};
    }

    void emit(T value) const {
        m_impl->emit(value);
    }

    void close_all() {
        m_impl->close_all();
    }

private:
    std::shared_ptr<detail::observable_impl<T>> m_impl;
};

void basic_subscription() {
    using namespace std::chrono_literals;

    asio::io_context context{};

    observable<int> observable{context};
    auto subscription = observable.subscribe();

    asio::co_spawn(
        context,
        [observable]() -> asio::awaitable<void> {
            for (int i = 1; i <= 100; i++) {
                observable.emit(i);
            }
            co_return;
        },
        asio::detached
    );

    asio::co_spawn(
        context,
        [subscription]() -> asio::awaitable<void> {
            for (int i = 0;; i++) {
                auto value = co_await subscription.next(5s);
                if (!value.has_value()) {
                    co_return;
                }
                
                std::cout << value.value() << std::endl;

                if (value == 50) {
                    subscription.unsubscribe();
                }
            }
        },
        asio::detached
    );

    context.run();
}

void subscription_timeout() {
    using namespace std::chrono_literals;

    asio::io_context context{};

    observable<int> observable{context};
    auto subscription = observable.subscribe();

    asio::co_spawn(
        context,
        [observable, subscription]() -> asio::awaitable<void> {
            co_await subscription.next(3s);

            std::cout << "timed out" << std::endl;

            observable.emit(123);

            const auto value = co_await subscription.next(3s);
            if (value.has_value()) {
                std::cout << value.value() << std::endl;
            }
        },
        asio::detached
    );

    context.run();
}

int main(int, char*[]) {
    basic_subscription();
    subscription_timeout();
    return EXIT_SUCCESS;
}