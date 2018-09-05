#include "client.h"
#include <chrono>

const int num = 1000000;
int main(int argc, char ** argv)
{
    Client client(argv[1], 8888);

    std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
    start = std::chrono::high_resolution_clock::now();
    for(int i=0;i<num;i++)
    {
        client.call(std::to_string(i));
    }
    end = std::chrono::system_clock::now();
    printf("Latency %.6lfus\n", 1e-3*(std::chrono::time_point_cast<std::chrono::nanoseconds>(end)-std::chrono::time_point_cast<std::chrono::nanoseconds>(start)).count()/num);
    return 0;
}

