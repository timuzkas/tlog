#include "tlog.hpp"
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

using namespace std;

void microservice_b() {
  T_SCOPE("service_b_internal");
  this_thread::sleep_for(chrono::milliseconds(2));
  T_INFO("Calculation finished");
}

void microservice_a(int id) {
  T_SCOPE("service_a_gateway");
  if (id % 1000 == 0) {
    T_ERR("Critical: Upstream service timeout");
    return;
  }
  T_INFO("Forwarding request to B");
  microservice_b();
}

void db_op(int id) {
  T_SCOPE("postgres_query");
  this_thread::sleep_for(chrono::milliseconds(1));
  if (id == 420) T_ERR("Deadlock detected");
  else T_INFO("Row updated");
}

void run_request(int id) {
  T_SCOPE("http_ingress");
  
  T_INFO("Incoming req " + to_string(id));
  
  thread t_a(microservice_a, id);
  db_op(id);
  t_a.join();

  if (id % 500 == 0) T_WARN("High latency detected on cleanup");
}

int main() {
  tlog::Logger::get().open("stress.log");
  
  const int total = 10000;
  const int concurrency = 16;
  atomic<int> count{0};
  vector<thread> workers;

  auto start = chrono::high_resolution_clock::now();

  for (int i = 0; i < concurrency; ++i) {
    workers.emplace_back([&] {
      while (true) {
        int id = count.fetch_add(1);
        if (id >= total) break;
        run_request(id);
      }
    });
  }

  for (auto &t : workers) t.join();

  auto end = chrono::high_resolution_clock::now();
  auto ms = chrono::duration_cast<chrono::milliseconds>(end - start).count();

  printf("Finished 10k requests in %lldms\n", ms);
  return 0;
}
