#include <queue>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <deque>
using namespace std; 

class OrderBook {
    struct Order {
        double price;
        int quantity;
        int id; 
        bool isValid; 

        Order(double p, int q, int OoderID)
            : price(p), quantity(q), id(orderID), isValid(true) {}


    };
    // maps orders by ID to an order object 
    unordered_map<int, shared_ptr<Order>> ordersByID; 
    unordered_map<double, deque<shared_ptr<Order>>> bidLevels;
    unordered_map<double, deque<shared_ptr<Order>>> askLevels;
    priority_queue<double> bids;
    priority_queue<double, vector<double>, greater<double>> asks;

public: 
    void marketOrder(bool buy, int qty) {

    }

    void limitOrder(bool buy, double price, int qty, int id) {

    }

    //looks for order by id in map and if found, sets isValid to false and quantity to 0
    void cancelOrder(int id) {
        auto it = ordersByID.find(id); 
        if (it == ordersByID.end()) {
            return;
        } 

        it->second->isValid = false; 
        it->second->quantity = 0; 
    }

};