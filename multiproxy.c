#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 10
#define MAXBUF 65535
#define MAX_CONNECTIONS 1024

#ifdef LOG
#define PRINT 1
#else
#define PRINT 0
#endif

typedef struct proxy
{
    char localport[256];
    char hostname[256];
    char hostport[256];
    int serverfd;
} proxy;

typedef struct buff
{
    int start;
    int left;
    char content[MAXBUF];
} buff;

typedef struct connection
{
    // Local R, Local W, Remote R, Remote W
    int fd[4];
    int ready[4];
    proxy *p;
    char ip[INET6_ADDRSTRLEN + 1];
    buff buffer[2];
} connection;

char error[256];
connection connections[MAX_CONNECTIONS];
int connCount;
int epollfd;
int proxiesCount;
proxy *proxies;
struct epoll_event events[MAX_CONNECTIONS];

connection *FindEmpty();
int CreateClientSocket(char *address, char *port);
int CreateServerSocket(char *address, char *port);
int ParseArgs(int *argc, char **argv, proxy *proxies);
proxy *FindProxy(int fd);
void AddConnection(int serverfd);
void AddToPoll(int flags, int fd);
void CloseConnection(connection *c);
void DeleteFd(int fd);
void PrintError(char *function);
void SendRecv(int from, int to, int *readReady, int *writeReady, buff *b, connection *c);
void SetNonBlocking(int fd);
void SetReady(int fd);

int main(int argc, char **argv)
{
    int fds, found, serverfd;
    proxy p[argc];

    memset(p, 0, argc);
    proxies = p;
    proxiesCount = ParseArgs(&argc, argv, proxies);

    if (proxiesCount >= MAX_CONNECTIONS)
    {
        printf("Too much proxies!\nLoaded: %d, limit: %d\n", proxiesCount, MAX_CONNECTIONS - 1);
        exit(0);
    }
    else
    {
        printf("Loaded proxies: %d\n", proxiesCount);
    }
    

    if (((epollfd = epoll_create1(0)) == -1))
    {
        PrintError("epoll_create()");
    }

    for (int i = 0; i < proxiesCount; i++)
    {
        serverfd = CreateServerSocket(NULL, p[i].localport);

        connections[i].fd[0] = serverfd;
        connections[i].p = proxies + i;
        proxies[i].serverfd = serverfd;

        AddToPoll(EPOLLIN, serverfd);
    }

    while (1)
    {
        if ((fds = epoll_wait(epollfd, events, MAX_CONNECTIONS, -1)) == -1)
        {
            PrintError("epoll_wait()");
        }

        for (int i = 0; i < fds; i++)
        {
            if ((events[i].events & EPOLLIN) || (events[i].events & EPOLLOUT))
            {
                SetReady(events[i].data.fd);
            }
        }

        for (int i = 0; i < proxiesCount; i++)
        {
            if (connections[i].ready[0])
            {
                connections[i].ready[0] = 0;
                AddConnection(connections[i].fd[0]);
            }
        }

        found = 0;

        for (int i = proxiesCount; i < MAX_CONNECTIONS; i++)
        {
            if (found == connCount)
            {
                break;
            }

            if (connections[i].p == NULL)
            {
                continue;
            }

            if (connections[i].ready[0] && connections[i].ready[3])
            {
                SendRecv(connections[i].fd[0], connections[i].fd[3], &connections[i].ready[0], &connections[i].ready[3], &connections[i].buffer[1], connections + i);
            }
            else if (connections[i].ready[1] && connections[i].ready[2])
            {
                SendRecv(connections[i].fd[2], connections[i].fd[1], &connections[i].ready[2], &connections[i].ready[1], &connections[i].buffer[0], connections + i);
            }

            found++;
        }
    }

    return 0;
}

connection *FindEmpty()
{
    for (int i = proxiesCount; i < MAX_CONNECTIONS; i++)
    {
        if (connections[i].p == NULL)
        {
            return connections + i;
        }
    }

    return NULL;
}

int CreateClientSocket(char *address, char *port)
{
    int sockfd, gai_error;
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((gai_error = getaddrinfo(address, port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "multiproxy: error in getaddrinfo(): %s\n", gai_strerror(gai_error));
        exit(0);
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (sockfd == -1)
        {
            perror("multiproxy: error in socket()");
            continue;
        }

        SetNonBlocking(sockfd);

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1 && errno != EINPROGRESS)
        {
            perror("multiproxy: error in connect()");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "multiproxy: error could not connect\n");
        exit(0);
    }

    freeaddrinfo(servinfo);
    return sockfd;
}

int CreateServerSocket(char *address, char *port)
{
    int sockfd, gai_error;
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((gai_error = getaddrinfo(address, port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "multiproxy: error in getaddrinfo(): %s\n", gai_strerror(gai_error));
        exit(0);
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (sockfd == -1)
        {
            perror("multiproxy: error in socket()");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1)
        {
            PrintError("setsockopt()");
        }

        SetNonBlocking(sockfd);

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("multiproxy: error in bind()");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "multiproxy: error could not bind\n");
        exit(0);
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        PrintError("listen()");
    }

    freeaddrinfo(servinfo);
    return sockfd;
}

int ParseArgs(int *argc, char **argv, proxy *proxies)
{
    if (*argc == 1)
    {
        printf("Usage:\n");
        printf("\t%s local_port:host:host_port ...\n", argv[0]);
        exit(0);
    }

    int count = 0;

    for (int i = 1; i < *argc; i++)
    {
        proxy p = {};
        char *token = strtok(argv[i], ":");
        int valid = 1;

        for (int j = 0; j < 3; j++)
        {
            if (token == NULL)
            {
                valid = 0;
                break;
            }

            switch (j)
            {
            case 0:
                memcpy(p.localport, token, strlen(token));
                break;
            case 1:
                memcpy(p.hostname, token, strlen(token));
                break;
            case 2:
                memcpy(p.hostport, token, strlen(token));
                break;
            }

            token = strtok(NULL, ":");
        }

        if (valid)
        {
            proxies[count] = p;
            count++;
        }
    }

    return count;
}

proxy *FindProxy(int fd)
{
    for (proxy *p = proxies; p != NULL && p->hostname != NULL; p++)
    {
        if (p->serverfd == fd)
        {
            return p;
        }
    }

    return NULL;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void AddConnection(int serverfd)
{
    int localfd, remotefd, localdup, remotedup;
    connection *empty;
    struct sockaddr_storage their_addr;
    socklen_t sin_size = sizeof their_addr;

    proxy *p = FindProxy(serverfd);

    if (p == NULL)
    {
        PrintError("FindProxy()");
    }

    if ((localfd = accept(serverfd, (struct sockaddr *)&their_addr, &sin_size)) == -1)
    {
        PrintError("accept()");
    }

    SetNonBlocking(localfd);

    if ((empty = FindEmpty()) == NULL)
    {
        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), error, sizeof(error));

        if (PRINT)
        {
            printf("Dropped connection from %s to %s -> %s:%s, fd %d\n", error, p->localport, p->hostname, p->hostport, localfd);
        }

        close(localfd);
        return;
    }

    remotefd = CreateClientSocket(p->hostname, p->hostport);

    AddToPoll(EPOLLIN | EPOLLET, localfd);
    AddToPoll(EPOLLIN | EPOLLET, remotefd);

    if (((localdup = dup(localfd)) == -1) || ((remotedup = dup(remotefd)) == -1))
    {
        PrintError("dup()");
    }

    AddToPoll(EPOLLOUT | EPOLLET, localdup);
    AddToPoll(EPOLLOUT | EPOLLET, remotedup);

    empty->fd[0] = localfd;
    empty->fd[1] = localdup;
    empty->fd[2] = remotefd;
    empty->fd[3] = remotedup;
    empty->p = p;

    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), empty->ip, sizeof(empty->ip));

    connCount++;

    if (PRINT)
    {
        printf("New connection from %s to %s -> %s:%s, (%d, %d) -> (%d, %d)\n", empty->ip, p->localport, p->hostname, p->hostport, empty->fd[0], empty->fd[1], empty->fd[2], empty->fd[3]);
    }
}

void AddToPoll(int flags, int fd)
{
    struct epoll_event ev;
    ev.events = flags;
    ev.data.fd = fd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        PrintError("epoll_ctl()");
    }
}

void CloseConnection(connection *c)
{
    DeleteFd(c->fd[0]);
    DeleteFd(c->fd[1]);
    DeleteFd(c->fd[2]);
    DeleteFd(c->fd[3]);

    close(c->fd[0]);
    close(c->fd[1]);
    close(c->fd[2]);
    close(c->fd[3]);

    if (PRINT)
    {
        printf("Closed connection from %s to %s -> %s:%s, (%d, %d) -> (%d, %d)\n", c->ip, c->p->localport, c->p->hostname, c->p->hostport, c->fd[0], c->fd[1], c->fd[2], c->fd[3]);
    }

    memset(c, 0, sizeof(connection));

    connCount--;
}

void DeleteFd(int fd)
{
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) != 0)
    {
        PrintError("epoll_ctl");
    }
}

void PrintError(char *function)
{
    sprintf(error, "multiproxy: error in %s", function);
    perror(error);

    exit(0);
}

void SendRecv(int from, int to, int *readReady, int *writeReady, buff *b, connection *c)
{
    int k, n;

    while (1)
    {
        if (b->left != 0)
        {
            if ((k = send(to, b->content + b->start, b->left, 0)) == b->left)
            {
                b->left = 0;
                b->start = 0;
            }
            else if (k == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    *writeReady = 0;
                }
                else
                {
                    sprintf(error, "multiproxy: error in write(%d)", to);
                    perror(error);
                    CloseConnection(c);
                }

                return;
            }
            else
            {
                *writeReady = 0;
                b->start += k;
                b->left -= k;

                return;
            }
        }

        if ((n = read(from, b->content, MAXBUF)) > 0)
        {
            b->start = 0;
            b->left = n;
        }
        else if (n == 0)
        {
            CloseConnection(c);
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                *readReady = 0;
            }
            else
            {
                sprintf(error, "multiproxy: error in read(%d)", from);
                perror(error);
                CloseConnection(c);
            }

            return;
        }
    }
}

void SetNonBlocking(int fd)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1)
    {
        PrintError("fcntl(Get)");
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        PrintError("fcntl(Set)");
    }
}

void SetReady(int fd)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (connections[i].fd[j] == fd)
            {
                connections[i].ready[j] = 1;
                return;
            }
        }
    }
}
