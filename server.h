#pragma once

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <cstring>
#include <unistd.h>
#include <map>
#include <mutex>
#include <string>
#include <functional>
#include <ThreadPool.h>
#include "type.h"
#include "ucp.h"

class Server
{
public:
    Server(const uint16_t &server_port)
        : lsock(0), ucp()
    {
        lsock = socket(AF_INET, SOCK_STREAM, 0);
        CHKERR_THROW(lsock < 0, "socket()", lsock = 0);

        int optval = 1;
        int ret = setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        CHKERR_THROW(ret < 0, "server setsockopt()", {close(lsock);lsock=0;});

        struct sockaddr_in inaddr;
        inaddr.sin_family      = AF_INET;
        inaddr.sin_port        = htons(server_port);
        inaddr.sin_addr.s_addr = INADDR_ANY;
        memset(inaddr.sin_zero, 0, sizeof(inaddr.sin_zero));

        ret = ::bind(lsock, (struct sockaddr*)&inaddr, sizeof(inaddr));
        CHKERR_THROW(ret < 0, "server bind", {close(lsock);lsock=0;});

        ret = listen(lsock, 128);
        CHKERR_THROW(ret < 0, "server listen", {close(lsock);lsock=0;});

        fprintf(stdout, "Waiting for connection...\n");
    }

    ~Server()
    {
        if(lsock) close(lsock);
    }

    void run()
    {
        progschj::ThreadPool threadPool;
#ifdef SHARE_WORKER
        UCP::WORKER ucp_worker = ucp.create_ucp_worker();
        std::string local_addr = ucp_worker.get_worker_addr();
#endif
        while(true)
        {
            /* Accept next connection */
            int connfd = accept(lsock, NULL, NULL);
            CHKERR_PRINT(connfd < 0, "accept client", {continue;});
#ifdef SHARE_WORKER
            threadPool.enqueue(&this->run_conn, connfd, &ucp, &ucp_worker, local_addr);
#else
            threadPool.enqueue(&this->run_conn, connfd, ucp);
#endif
        }
        threadPool.wait_until_empty();
        threadPool.wait_until_nothing_in_flight();
    }

private:
    int lsock;
    UCP ucp;

#ifdef SHARE_WORKER
    static int run_conn(int connfd, UCP *ucp, UCP::WORKER *ucp_worker, std::string local_addr)
    {
#else
    static int run_conn(int connfd, UCP &ucp)
    {
        UCP::WORKER *ucp_worker = new UCP::WORKER(ucp.create_ucp_worker());
        std::string local_addr = ucp_worker->get_worker_addr();
#endif
        uint32_t addr_len;
        int ret = recv(connfd, &addr_len, sizeof(addr_len), MSG_WAITALL);
        CHKERR_PRINT((uint32_t)ret != sizeof(addr_len), "recv addr_len", {close(connfd);return 0;});
        addr_len = ntohl(addr_len);
        std::vector<char> recvAddr(addr_len);
        ret = recv(connfd, &(recvAddr[0]), addr_len, MSG_WAITALL);
        CHKERR_PRINT((uint32_t)ret != addr_len, "recv addr", {close(connfd);return 0;});
        std::string peer_addr(&(recvAddr[0]), recvAddr.size());

        uint32_t len = htonl(local_addr.size());
        std::string local_addr_send = std::string((char*)&len, sizeof(len))+local_addr;
        ret = send(connfd, local_addr_send.c_str(), local_addr_send.size(), 0);
        CHKERR_PRINT((uint32_t)ret != local_addr_send.size(), "send addr", {close(connfd);return 0;});

        UCP::EP *ucp_ep = new UCP::EP(ucp_worker->ucp_create_ep(peer_addr));

        size_t ep_tag = std::hash<std::string>()(local_addr+peer_addr);
        printf("EP tag : %lu\n", ep_tag);

        while(true)
        {

            std::string recv;
            try
            {
                recv = std::move(ucp_ep->recv(ep_tag));
                if(recv == std::string("CloseEP")) break;
            }
            catch(std::exception &e)
            {
                fprintf(stderr, "Faild to recv args : %s\n", e.what());
                break;
            }

            try
            {
                ucp_ep->send(recv, ep_tag);
            }
            catch(std::exception &e)
            {
                fprintf(stderr, "Faild to send ret : %s\n", e.what());
                break;
            }
        }

        close(connfd);
        delete ucp_ep;
#ifndef SHARE_WORKER
        delete ucp_worker;
#endif
        return 0;

    }

};
