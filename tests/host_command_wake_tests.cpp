#include "host_command_wake.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <future>
using namespace std::chrono_literals;
static void require(bool x,const char*m){if(!x)throw std::runtime_error(m);}
int main()try{using eii::host::deadlineWaitMilliseconds;
 require(deadlineWaitMilliseconds(-1ns)==0&&deadlineWaitMilliseconds(0ns)==0,"expired deadline");
 require(deadlineWaitMilliseconds(1ns)==1&&deadlineWaitMilliseconds(999999ns)==1,"fractional millisecond must block");
 require(deadlineWaitMilliseconds(1ms)==1&&deadlineWaitMilliseconds(1000001ns)==2,"ceil boundary");
 require(deadlineWaitMilliseconds(std::chrono::nanoseconds::max())==0xfffffffeu,"finite Windows timeout cap");
 eii::host::CommandWake wake;std::mutex mutex;bool ready=false;std::promise<void> entered;
 auto worker=std::async(std::launch::async,[&]{std::unique_lock lock(mutex);entered.set_value();wake.wait(lock,[&]{return ready;});});
 entered.get_future().wait();{std::lock_guard lock(mutex);ready=true;}wake.notify_one();require(worker.wait_for(2s)==std::future_status::ready,"notification lost");worker.get();
 {std::unique_lock lock(mutex);auto start=std::chrono::steady_clock::now();require(!wake.wait_until(lock,start+350us,[]{return false;}),"timeout result");require(std::chrono::steady_clock::now()>=start+350us,"early timeout");}
 // A notification already sent before entering the wait must be harmless;
 // the protected predicate owns readiness, including pause/shutdown commands.
 {std::lock_guard lock(mutex);ready=true;}wake.notify_one();{std::unique_lock lock(mutex);require(wake.wait_until(lock,std::chrono::steady_clock::now()+2s,[&]{return ready;}),"ready predicate");}
 std::cout<<"PASS: positive timeout ceil/cap; notification before/during wait; fractional deadline; protected predicate\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
