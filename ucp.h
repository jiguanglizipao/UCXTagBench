#pragma once

#include <cstring>
#include <ucp/api/ucp.h>
#include <vector>
#include <mutex>
#include "type.h"

#ifdef MUTEX
#define LOCK std::lock_guard<std::mutex> lock(worker_mutex)
#define WORKER_SHARE false
#else
#define LOCK 0
#define WORKER_SHARE true
#endif

class UCP
{
public:
    struct ucx_context 
    {
        int completed;
    };

    class EP;

    class WORKER
    {
    public:
        WORKER(ucp_worker_h &&_worker, ucp_context_h &_context)
            :worker(std::move(_worker)), context(_context), valid(true)
        {
        }

        WORKER(WORKER&& a)
            :worker(std::move(a.worker)), context(a.context), valid(a.valid)
        {
            a.valid = false;
        }

        std::string get_worker_addr()
        {
            LOCK;
            ucp_address_t *local_addr;
            size_t local_addr_len;
            ucs_status_t status = ucp_worker_get_address(worker, &local_addr, &local_addr_len);
            CHKERR_THROW(status != UCS_OK, "worker_get_address", {});
            return std::string((char*)local_addr, local_addr_len);
        }

        EP ucp_create_ep(const std::string &addr)
        {
            LOCK;
            const ucp_address_t *peer_addr = (const ucp_address_t *)addr.c_str();
            ucp_ep_params_t ep_params;
            ep_params.field_mask      = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
            ep_params.address         = peer_addr;
            ucp_ep_h ucp_ep;
            ucs_status_t status = ucp_ep_create(worker, &ep_params, &ucp_ep);
            CHKERR_THROW(status != UCS_OK, "ucp_ep_create", {});

            ucp_context_attr_t attr;
            status = ucp_context_query(context, &attr);
            CHKERR_THROW(status != UCS_OK, "ucp_ep_create", {});

            return EP(std::move(ucp_ep), worker, worker_mutex, attr.request_size);
        }

        void close()
        {
            LOCK;
            if(!valid) return;
            ucp_address_t *local_addr;
            size_t local_addr_len;
            ucp_worker_get_address(worker, &local_addr, &local_addr_len);
            ucp_worker_release_address(worker, local_addr);
            ucp_worker_destroy(worker);
            valid = false;
        }

        ~WORKER()
        {
            close();
        }

        std::mutex worker_mutex;

    private:
        ucp_worker_h worker;
        ucp_context_h &context;
        bool valid;
    };

    class EP
    {
        public:
            EP(ucp_ep_h &&_ep, ucp_worker_h &_worker, std::mutex &_worker_mutex, const size_t &_ucp_request_size)
                :ep(std::move(_ep)), worker(_worker), worker_mutex(_worker_mutex), ucp_request_size(_ucp_request_size), valid(true)
            {
            }


            void send(const char *data, const size_t &len, const ucp_tag_t &tag = 1llu)
            {
                CHKERR_THROW(!valid, "send UCX data message", {});
                ucx_context *request;

                {
                    LOCK;
                    request = (ucx_context*)ucp_tag_send_nb(ep, data, len, ucp_dt_make_contig(1), tag, send_handle);
                }

                if (UCS_PTR_IS_ERR(request)) 
                {
                    CHKERR_THROW(true, "send UCX data message", {});
                } 
                else if (UCS_PTR_STATUS(request) != UCS_OK) {
                    wait(request);
                    request->completed = 0;
                    {
                        LOCK;
                        ucp_request_release(request);
                    }
                }

            }

            void send(const std::string &str, const ucp_tag_t &tag = 1llu)
            {
                send(str.c_str(), str.size(), tag);
            }

            std::string recv(const ucp_tag_t &tag = 1llu, const ucp_tag_t &tag_mask = 0xffffffffffffffffllu)
            {
                CHKERR_THROW(!valid, "receive UCX data message", {});

                ucp_tag_recv_info_t info_tag;
                ucp_tag_message_h msg_tag;
                ucs_status_t status;

                /* Receive test string from server */
                for(;;) 
                {
                    LOCK;
                    /* Probing incoming events in non-block mode */
                    msg_tag = ucp_tag_probe_nb(worker, tag, tag_mask, 1, &info_tag);
                    if(msg_tag != nullptr) 
                    {
                        /* Message arrived */
                        break;
                    } 
                    else if(ucp_worker_progress(worker)) 
                    {
                        /* Some events were polled; try again without going to sleep */
                        continue;
                    }
            
                    /* If we got here, ucp_worker_progress() returned 0, so we can sleep.
                     * Following blocked methods used to polling internal file descriptor
                     * to make CPU idle and don't spin loop
                     */
                    // status = ucp_worker_wait(worker);
                    // CHKERR_THROW(status != UCS_OK, "ucp_worker_wait", {});
                }

                std::vector<char> buffer(info_tag.length);

                ucx_context *request;
                {
                    LOCK;
                    request = (ucx_context*)ucp_tag_msg_recv_nb(worker, buffer.data(), info_tag.length, ucp_dt_make_contig(1), msg_tag, recv_handle);
                }
            
                if (UCS_PTR_IS_ERR(request)) 
                {
                    CHKERR_THROW(true, "receive UCX data message", {});
                } 
                else if (UCS_PTR_STATUS(request) != UCS_OK) 
                {
                    wait(request);
                    request->completed = 0;
                    {
                        LOCK;
                        ucp_request_release(request);
                    }
                }

                return std::string(buffer.data(), buffer.size());
            }

            void close()
            {
                if(!valid) return;
                ucx_context *request;
                {
                    LOCK;
                    request = (ucx_context*)ucp_ep_close_nb(ep, UCP_EP_CLOSE_MODE_FLUSH);
                }
                if(UCS_PTR_IS_ERR(request)) 
                {
                } 
                else if (UCS_PTR_STATUS(request) != UCS_OK) 
                {
                    ucs_status_t status;
                    do 
                    {
                        LOCK;
                        ucp_worker_progress(worker);
                        status = ucp_request_check_status(request);
                    } while (status == UCS_INPROGRESS);
                    LOCK;
                    ucp_request_release(request);
                }
                valid = false;
            }

            ~EP()
            {
                close();
            }

        private:
            ucp_ep_h ep;
            ucp_worker_h &worker;
            std::mutex &worker_mutex;
            const size_t ucp_request_size;
            bool valid;

            static void recv_handle(void *request, ucs_status_t status, ucp_tag_recv_info_t *info)
            {
                ucx_context *context = (ucx_context *) request;
                context->completed = 1;
            }

            static void send_handle(void *request, ucs_status_t status)
            {
                struct ucx_context *context = (struct ucx_context *) request;
                context->completed = 1;
            }

            static void flush_callback(void *request, ucs_status_t status)
            {
            }

            void wait(ucx_context *request)
            {
                while (request->completed == 0) 
                {
                    LOCK;
                    ucp_worker_progress(worker);
                }
            }

    };

    UCP()
    {
        try
        {
            init_ucp_context();
            is_valid = true;
        }
        catch(std::exception &e)
        {
            is_valid = false;
        }
    }

    WORKER create_ucp_worker()
    {
        ucs_status_t status;

        ucp_worker_params_t worker_params;

        worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
        worker_params.thread_mode = UCS_THREAD_MODE_MULTI;

        ucp_worker_h ucp_worker;
    
        status = ucp_worker_create(ucp_context, &worker_params, &ucp_worker);
        CHKERR_THROW(status != UCS_OK, "ucp_worker_create", ucp_cleanup(ucp_context));

        ucp_worker_print_info(ucp_worker, stdout);

        return WORKER(std::move(ucp_worker), ucp_context);

    }

    ~UCP()
    {
        if(is_valid) ucp_cleanup(ucp_context);
    }

private:
    bool is_valid;
    ucp_context_h ucp_context;

    static void request_init(void *request)
    {
        ucx_context *ctx = (ucx_context *) request;
        ctx->completed = 0;
    }

    void init_ucp_context()
    {
        /* UCP temporary vars */
        ucp_params_t ucp_params;
        ucp_config_t *config;
        ucs_status_t status;
    
        /* OOB connection vars */
        memset(&ucp_params, 0, sizeof(ucp_params));
    
        /* UCP initialization */
        status = ucp_config_read(nullptr, nullptr, &config);
        CHKERR_THROW(status != UCS_OK, "ucp_config_read", {});
    
        ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES |
                                UCP_PARAM_FIELD_MT_WORKERS_SHARED |
                                UCP_PARAM_FIELD_REQUEST_SIZE |
                                UCP_PARAM_FIELD_REQUEST_INIT;

        ucp_params.request_size = sizeof(ucx_context);
        ucp_params.request_init = request_init;

        ucp_params.mt_workers_shared = WORKER_SHARE;

        ucp_params.features = UCP_FEATURE_TAG;
        // ucp_params.features |= UCP_FEATURE_WAKEUP;

        status = ucp_init(&ucp_params, config, &ucp_context);
    
        ucp_config_print(config, stdout, nullptr, UCS_CONFIG_PRINT_CONFIG);
        ucp_context_print_info(ucp_context, stdout);
    
        ucp_config_release(config);
        CHKERR_THROW(status != UCS_OK, "ucp_init", {});

    }

};
