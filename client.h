#pragma once

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <cstring>
#include <unistd.h>
#include <string>
#include <cassert>
#include "type.h"
#include "ucp.h"

class Client
{
public:
    Client(const std::string &server_name, const uint16_t &server_port)
        :connfd(0), ucp(), ucp_worker(), ucp_ep()
    {
        connfd = socket(AF_INET, SOCK_STREAM, 0);
        CHKERR_THROW(connfd < 0, "socket()", connfd = 0);

        struct hostent *he = gethostbyname(server_name.c_str());
        CHKERR_THROW((he == NULL || he->h_addr_list == NULL), "found a host", {close(connfd);connfd=0;});

        struct sockaddr_in conn_addr;
        conn_addr.sin_family = he->h_addrtype;
        conn_addr.sin_port   = htons(server_port);
        memcpy(&conn_addr.sin_addr, he->h_addr_list[0], he->h_length);
        memset(conn_addr.sin_zero, 0, sizeof(conn_addr.sin_zero));

        int ret = connect(connfd, (struct sockaddr*)&conn_addr, sizeof(conn_addr));
        CHKERR_THROW(ret < 0, "connect client", {close(connfd);connfd=0;});
        
        try
        {
            ucp_worker = new UCP::WORKER(ucp.create_ucp_worker());
            std::string local_addr = ucp_worker->get_worker_addr();
            uint32_t len = htonl(local_addr.size());
            std::string local_addr_send = std::string((char*)&len, sizeof(len))+local_addr;
            ret = send(connfd, local_addr_send.c_str(), local_addr_send.size(), 0);
            CHKERR_THROW((uint32_t)ret != local_addr_send.size(), "send addr", {close(connfd);connfd=0;});

            uint32_t addr_len;
            ret = recv(connfd, &addr_len, sizeof(addr_len), MSG_WAITALL);
            CHKERR_THROW((uint32_t)ret != sizeof(addr_len), "recv addr_len", {close(connfd);connfd=0;});
            addr_len = ntohl(addr_len);
            std::vector<char> recvAddr(addr_len);
            ret = recv(connfd, &(recvAddr[0]), addr_len, MSG_WAITALL);
            CHKERR_THROW((uint32_t)ret != addr_len, "recv addr", {close(connfd);connfd=0;});
            std::string peer_addr(&(recvAddr[0]), recvAddr.size());

            ucp_ep = new UCP::EP(ucp_worker->ucp_create_ep(peer_addr));

            ep_tag = std::hash<std::string>()(peer_addr+local_addr);
            printf("EP tag : %lu\n", ep_tag);
        }
        catch(std::exception &e)
        {
            close(connfd);
            connfd=0;
            throw e;
        }

    }

    ~Client()
    {
        if(connfd) 
        {
            ucp_ep->send(std::string("CloseEP"), ep_tag);
            close(connfd);
            if(ucp_ep) delete ucp_ep;
            if(ucp_worker) delete ucp_worker;
        }
    }

    void call(const std::string &str)
    {
        try
        {
            ucp_ep->send(str, ep_tag);
        }
        catch(std::exception &e)
        {
            fprintf(stderr, "Faild to send str : %s\n", e.what());
            return;
        }

        std::string recv;
        try
        {
            recv = std::move(ucp_ep->recv(ep_tag));
        }
        catch(std::exception &e)
        {
            fprintf(stderr, "Faild to recv str : %s\n", e.what());
            return;
        }

        assert(send == recv);

    }

private:
    int connfd;
    UCP ucp;
    UCP::WORKER *ucp_worker;
    UCP::EP *ucp_ep;
    size_t ep_tag;

};

