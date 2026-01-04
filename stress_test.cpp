#include "tlog.hpp"
#include <vector>
#include <iostream>

void db_operation(int depth) {
    T_SCOPE("database_query");
    T_TAG("op_type", "select");
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    
    if (depth > 0 && rand() % 5 == 0) {
        T_SCOPE("cache_lookup");
        T_INFO("cache miss - fetching from disk");
    }
}

void external_api_call() {
    T_SCOPE("api_gateway_request");
    T_TAG("endpoint", "/v1/validate");
    
    if (rand() % 20 == 0) {
        T_ERR("connection reset by peer");
    } else {
        T_INFO("payload delivered");
    }
}

void process_request(int id) {
    T_SCOPE("http_request");
    T_TAG("request_id", std::to_string(id));
    T_TAG("user_id", std::to_string(rand() % 1000));
    T_TAG("region", (rand() % 2 == 0 ? "us-east" : "eu-west"));

    {
        T_SCOPE("auth_middleware");
        std::this_thread::sleep_for(std::chrono::microseconds(5));
        T_INFO("token validated");
    }

    for (int i = 0; i < (1 + rand() % 3); ++i) {
        db_operation(i);
    }

    if (rand() % 10 == 0) {
        external_api_call();
    }

    T_INFO("response sent 200 OK");
}

int main(int argc, char** argv) {
    int iterations = (argc > 1) ? std::stoi(argv[1]) : 10000;
    
    T_INIT("stress.log");
    T_SAMPLE(1.0); // Sample everything for stress testing

    std::cout << "Generating " << iterations << " traces to stress.log..." << std::endl;

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < iterations / 4; ++i) {
                process_request(t * 100000 + i);
            }
        });
    }

    for (auto& w : workers) w.join();

    std::cout << "Done. Closing logger..." << std::endl;
    return 0;
}
