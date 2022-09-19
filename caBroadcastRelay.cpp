#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#define socklen_t int
#include <cstdint>
#else
#define SOCKET int
#define closesocket close
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#endif /* ifdef _WIN32 */
#include <stdio.h>
#include <iostream>
#include <list>
#include <algorithm>
#include <cstdlib>
#include <cstring>

struct ConnectionDetails
{
    SOCKET sock;
    time_t update_time;
    struct sockaddr_in reply_to; 
    ConnectionDetails(const SOCKET& sock_, const struct sockaddr_in& reply_to_) : sock(sock_) { time(&update_time); memcpy(&reply_to, &reply_to_, sizeof(struct sockaddr_in )); }
    ConnectionDetails(const ConnectionDetails& cd) : sock(cd.sock), update_time(cd.update_time) { memcpy(&reply_to, &(cd.reply_to), sizeof(struct sockaddr_in )); }
    void print() const
    {
        struct sockaddr_in bind_addr;
        socklen_t bind_len = sizeof(struct sockaddr_in);
        getsockname(sock, (struct sockaddr *)&bind_addr, &bind_len);
        std::cerr << "Remote endpoint " << inet_ntoa(reply_to.sin_addr) << " port " << ntohs(reply_to.sin_port) << std::endl;  
        std::cerr << "Reply endpoint " << inet_ntoa(bind_addr.sin_addr) << " port " << ntohs(bind_addr.sin_port) << std::endl; 
    }
};

typedef std::list<ConnectionDetails> connection_t;

// listen for unicast udp packets on the port specified in argv on all interfaecs except loopback
// forward packets to loopback on standard epics name query port (5064) and then
// forward any the reply back to the original sender.
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: caBroadcastRelay port" << std::endl;
        return -1;
    }
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif /* ifdef _WIN32 */

    uint16_t udp_listen_port = static_cast<uint16_t>(atol(argv[1]));
    uint16_t udp_forward_port = 5064;
    double cleanup_timeout = 30.0;
    
    connection_t connection_details;
    struct sockaddr_in from_addr, to_addr, to_reply_addr;

    SOCKET from_socket; // where we listed for name queries
    if ((from_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("create from_socket");
        exit(1);
    };

    int one (1);

    // listen for unicast UDP to our port on all interfaces
    memset(&from_addr, 0, sizeof(struct sockaddr_in));
    from_addr.sin_family = AF_INET;
    from_addr.sin_port = htons(udp_listen_port);
    from_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(from_socket, (struct sockaddr *) &from_addr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind from_socket");
        exit(1);
    }

    // we send name queries to loopback
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(udp_forward_port);
    to_addr.sin_addr.s_addr = inet_addr("127.255.255.255");

    // bind to loopback for replies to items we have forwarded so can then send to correct final recipient 
    memset(&to_reply_addr, 0, sizeof(to_reply_addr));
    to_reply_addr.sin_family = AF_INET;
    to_reply_addr.sin_port = 0; // dynamically allocated
    to_reply_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buffer[4096];

    fd_set readfds;
    while (true) {
        FD_ZERO(&readfds);
        FD_SET(from_socket, &readfds);
        int maxfd = static_cast<int>(from_socket);
        for(connection_t::const_iterator it = connection_details.begin(); it != connection_details.end(); ++it)
        {
            FD_SET(it->sock, &readfds);
            maxfd = std::max((int)it->sock, maxfd);
        }
        struct timeval tv = { 1, 0 };
        int nready = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (nready < 0) {
            perror("select");
            exit(1);
        }
       
        if (FD_ISSET(from_socket, &readfds)) // new name query
        {
            struct sockaddr_in recv_addr;
            socklen_t recv_size = sizeof(recv_addr); 
            int len = recvfrom(from_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&recv_addr, &recv_size);
            if (len >= 0)
            {
                SOCKET to_socket;
                std::cerr << "Received query size " << len << " from " << inet_ntoa(recv_addr.sin_addr) << " port " << ntohs(recv_addr.sin_port) << std::endl;
                // create a new socket to forward query and wit for replies 
                if ((to_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
                    perror("create to_socket");
                    exit(1);
                };
                if (setsockopt(to_socket, SOL_SOCKET, SO_BROADCAST, (char*) &one, sizeof(int)) < 0) {
                    perror("SO_BROADCAST on to_socket");
                    exit(1);
                }
                if (bind(to_socket, (struct sockaddr *) &to_reply_addr, sizeof(struct sockaddr_in)) < 0) {
                    perror("bind to_socket");
                    exit(1);
                }
                //if (connect(to_socket, (struct sockaddr *) &to_addr, sizeof(struct sockaddr_in)) < 0) {
                //      perror("connect to_socket");
                //      exit(1);
                //}
                struct sockaddr_in bind_addr;
                socklen_t bind_len = sizeof(struct sockaddr_in);
                getsockname(to_socket, (struct sockaddr *)&bind_addr, &bind_len);
                std::cerr << "Creating reply endpoint " << inet_ntoa(bind_addr.sin_addr) << " port " << ntohs(bind_addr.sin_port) << std::endl; 
                std::cerr << "sending to " << inet_ntoa(to_addr.sin_addr) << " port " << ntohs(to_addr.sin_port) << std::endl; 
                if (sendto(to_socket, buffer, len, 0, (struct sockaddr*)&to_addr, sizeof(struct sockaddr_in)) < 0)
                {
                    perror("sendto to_socket");
                }
                //if (send(to_socket, buffer, len, 0) < 0)
                //{
                //    perror("sendto to_socket");
                //}
                connection_details.push_back(ConnectionDetails(to_socket, recv_addr));
            }
            else
            {
                perror("recvfrom from_socket");
            }
        }
        for(connection_t::iterator it = connection_details.begin(); it != connection_details.end(); ++it)
        {
            if (FD_ISSET(it->sock, &readfds)) // have a reply to a forwarded query
            {
                time(&(it->update_time));
                struct sockaddr_in recv_addr;
                socklen_t recv_size = sizeof(recv_addr); 
                int len = recvfrom(it->sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&recv_addr, &recv_size);
                if (len >= 0)
                {
                    std::cerr << "Sending reply size " << len << " to " << inet_ntoa(it->reply_to.sin_addr) << " port " << ntohs(it->reply_to.sin_port) << std::endl;
                    if (sendto(from_socket, buffer, len, 0, (struct sockaddr*)&(it->reply_to), sizeof(struct sockaddr_in)) < 0)
                    {
                        perror("sendto from_socket");
                    }
                }
                else
                {
                    perror("recvfrom it->sock");
                }
            }
        }
        bool done = false;
        while(!done)
        {            
            time_t now;
            time(&now);
            done = true;
            for(connection_t::iterator it = connection_details.begin(); it != connection_details.end(); ++it)
            {
                if (difftime(now, it->update_time) > cleanup_timeout)
                {
                    std::cerr << "Cleaning" << std::endl; 
                    it->print();
                    closesocket(it->sock);
                    connection_details.erase(it); // this seems to invalidate iterators
                    done = false;
                    break;
                }
            }
        }
    }
    return 0;
}
