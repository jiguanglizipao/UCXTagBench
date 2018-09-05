#include "server.h"

int main()
{
    Server server(8888);
    server.run();
    return 0;
}

