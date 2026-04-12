#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <deque>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
using namespace std;

constexpr double MARKET_PRICE = 0.0;


struct Order {
    string order_id;
    bool is_buy;
    int quantity;
    double price;
    long timestamp;
    bool is_active;

    Order(const string& id, bool buy, int qty, double p, long ts)
        : order_id(id), is_buy(buy), quantity(qty), price(p),
          timestamp(ts), is_active(true) {}
};

enum class OrderType { LIMIT, MARKET, CANCEL };

struct OrderRequest {
    OrderType type;
    string order_id;
    bool is_buy   = false;
    double price  = 0.0;
    int quantity  = 0;
};


class OrderBook {
    unordered_map<string, shared_ptr<Order>> order_map;
    unordered_map<double, deque<shared_ptr<Order>>> bid_levels;
    unordered_map<double, deque<shared_ptr<Order>>> ask_levels;
    priority_queue<double> bids;
    priority_queue<double, vector<double>, greater<double>> asks;
    long next_timestamp = 0;

    bool clean_front(deque<shared_ptr<Order>>& level) {
        while (!level.empty() && !level.front()->is_active) {
            level.pop_front();
        }
        return !level.empty();
    }

    template<typename Heap>
    void match_against(shared_ptr<Order>& incoming, Heap& opposite_heap,
                       unordered_map<double, deque<shared_ptr<Order>>>& opposite_levels) {
        while (incoming->quantity > 0 && !opposite_heap.empty()) {
            double best_price = opposite_heap.top();

            if (incoming->price != MARKET_PRICE) {
                if (incoming->is_buy  && best_price > incoming->price) break;
                if (!incoming->is_buy && best_price < incoming->price) break;
            }

            auto level_it = opposite_levels.find(best_price);
            if (level_it == opposite_levels.end() || !clean_front(level_it->second)) {
                opposite_heap.pop();
                if (level_it != opposite_levels.end()) {
                    opposite_levels.erase(level_it);
                }
                continue;
            }

            auto& level = level_it->second;

            while (incoming->quantity > 0 && clean_front(level)) {
                auto& resting  = level.front();
                int fill_qty   = min(incoming->quantity, resting->quantity);

                cout << "FILL: " << fill_qty << " @ " << best_price
                     << " (maker=" << resting->order_id
                     << ", taker=" << incoming->order_id << ")\n";

                incoming->quantity -= fill_qty;
                resting->quantity  -= fill_qty;

                if (resting->quantity == 0) {
                    resting->is_active = false;
                    order_map.erase(resting->order_id);
                    level.pop_front();
                }
            }

            if (level.empty()) {
                opposite_levels.erase(level_it);
                opposite_heap.pop();
            }
        }
    }

    void match_order(shared_ptr<Order>& incoming) {
        if (incoming->is_buy) {
            match_against(incoming, asks, ask_levels);
        } else {
            match_against(incoming, bids, bid_levels);
        }
    }

    void insert_into_book(shared_ptr<Order>& order) {
        double price   = order->price;
        auto& levels   = order->is_buy ? bid_levels : ask_levels;

        bool new_level = (levels.find(price) == levels.end() || levels[price].empty());
        levels[price].push_back(order);
        order_map[order->order_id] = order;

        if (new_level) {
            if (order->is_buy) bids.push(price);
            else               asks.push(price);
        }
    }

public:
    void add_limit_order(const string& order_id, bool is_buy, double price, int quantity) {
        auto order = make_shared<Order>(order_id, is_buy, quantity, price, next_timestamp++);
        match_order(order);
        if (order->quantity > 0) insert_into_book(order);
    }

    void add_market_order(const string& order_id, bool is_buy, int quantity) {
        auto order = make_shared<Order>(order_id, is_buy, quantity, MARKET_PRICE, next_timestamp++);
        match_order(order);
        if (order->quantity > 0) {
            cout << "PARTIAL: " << order->order_id
                 << " has " << order->quantity << " unfilled\n";
        }
    }

    void cancel_order(const string& order_id) {
        auto it = order_map.find(order_id);
        if (it == order_map.end()) return;
        it->second->is_active = false;
        order_map.erase(it);
    }

    int get_order_quantity(const string& order_id) const {
        auto it = order_map.find(order_id);
        if (it == order_map.end()) return -1;
        return it->second->quantity;
    }

    size_t active_order_count() const { return order_map.size(); }
};


std::queue<OrderRequest> order_queue;
std::mutex queueMutex;
std::condition_variable queueCV;
std::atomic<bool> done{false};

void enqueueOrder(const OrderRequest& req) {
    {
        lock_guard<mutex> lock(queueMutex);
        order_queue.push(req);
    }
    queueCV.notify_one();
}

//this is the only thing that acc modifies the orderbook

void processOrders(OrderBook& book) {
    while (true) {
        OrderRequest req;
        {
            unique_lock<mutex> lock(queueMutex);
            queueCV.wait(lock, [] { return !order_queue.empty() || done.load(); });

            if (order_queue.empty()) break;   // done == true and queue drained

            req = order_queue.front();
            order_queue.pop();
        }

        switch (req.type) {
            case OrderType::LIMIT:
                book.add_limit_order(req.order_id, req.is_buy, req.price, req.quantity);
                break;
            case OrderType::MARKET:
                book.add_market_order(req.order_id, req.is_buy, req.quantity);
                break;
            case OrderType::CANCEL:
                book.cancel_order(req.order_id);
                break;
        }
    }
}



int main() {
    OrderBook book;
    OrderBook book2;

    thread worker(processOrders, ref(book));
    thread worker2(processOrders, ref(book2));

    enqueueOrder({ OrderType::LIMIT,  "L1", false, 100.0, 10 });
    enqueueOrder({ OrderType::LIMIT,  "L2", false, 101.0,  5 });
    enqueueOrder({ OrderType::LIMIT,  "L3", true,   99.0,  7 });
    enqueueOrder({ OrderType::LIMIT,  "L4", true,  100.0,  3 });
    enqueueOrder({ OrderType::CANCEL, "L3" });
    enqueueOrder({ OrderType::MARKET, "M1", true,  0.0,   12 });

    done = true;
    queueCV.notify_one();

    worker.join();

    return 0;
}
