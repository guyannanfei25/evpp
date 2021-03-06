#include "evpp/inner_pre.h"

#include "evpp/libevent_headers.h"
#include "evpp/sockets.h"
#include "evpp/duration.h"

namespace evpp {

static const std::string empty_string;

std::string strerror(int e) {
#ifdef H_OS_WINDOWS
    LPVOID buf = NULL;
    ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL);

    if (buf) {
        std::string s = (char*)buf;
        LocalFree(buf);
        return s;
    }

    return empty_string;
#else
    char buf[2048] = {};
    strerror_r(e, buf, sizeof(buf) - 1);
    return std::string(buf);
#endif
}

namespace sock {
int CreateNonblockingSocket() {
    int serrno = 0;

    /* Create listen socket */
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        serrno = errno;
        LOG_ERROR << "socket error " << strerror(serrno);
        return INVALID_SOCKET;
    }

    if (evutil_make_socket_nonblocking(fd) < 0) {
        goto out;
    }

#ifndef H_OS_WINDOWS
    if (fcntl(fd, F_SETFD, 1) == -1) {
        serrno = errno;
        LOG_FATAL << "fcntl(F_SETFD)" << strerror(serrno);
        goto out;
    }
#endif

    SetKeepAlive(fd);
    SetReuseAddr(fd);
    SetReusePort(fd);
    return fd;
out:
    EVUTIL_CLOSESOCKET(fd);
    return INVALID_SOCKET;
}

int CreateUDPServer(int port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        int serrno = errno;
        LOG_ERROR << "socket error " << strerror(serrno);
        return INVALID_SOCKET;
    }
    SetReuseAddr(fd);

    std::string addr = std::string("0.0.0.0:") + std::to_string(port);
    struct sockaddr_in local = ParseFromIPPort(addr.c_str());
    if (::bind(fd, (struct sockaddr*)&local, sizeof(local))) {
        int serrno = errno;
        LOG_ERROR << "socket bind error=" << serrno << " " << strerror(serrno);
        return INVALID_SOCKET;
    }

    return fd;
}

struct sockaddr_in ParseFromIPPort(const char* address/*ip:port*/) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    std::string a = address;
    size_t index = a.rfind(':');
    if (index == std::string::npos) {
        LOG_FATAL << "Address specified error [" << address << "]";
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(::atoi(&a[index + 1]));
    a[index] = '\0';

    int rc = ::inet_pton(AF_INET, a.data(), &addr.sin_addr);
    if (rc == 0) {
        LOG_INFO << "ParseFromIPPort inet_pton(AF_INET '" << a.data() << "', ...) rc=0. " << a.data() << " is not a valid IP address. Maybe it is a hostname.";
    } else if (rc < 0) {
        int serrno = errno;
        if (serrno == 0) {
            LOG_INFO << "[" << a.data() << "] is not a IP address. Maybe it is a hostname.";
        } else {
            LOG_WARN << "ParseFromIPPort inet_pton(AF_INET, '" << a.data() << "', ...) failed : " << strerror(serrno);
        }
    }

    //TODO add ipv6 support

    return addr;
}

struct sockaddr_in GetLocalAddr(int sockfd) {
    struct sockaddr_in laddr;
    memset(&laddr, 0, sizeof laddr);
    socklen_t addrlen = static_cast<socklen_t>(sizeof laddr);

    if (::getsockname(sockfd, sockaddr_cast(&laddr), &addrlen) < 0) {
        LOG_ERROR << "GetLocalAddr:" << strerror(errno);
        memset(&laddr, 0, sizeof laddr);
    }

    return laddr;
}

std::string ToIPPort(const struct sockaddr_storage* ss) {
    std::string saddr;
    int port = 0;

    if (ss->ss_family == AF_INET) {
        struct sockaddr_in* addr4 = const_cast<struct sockaddr_in*>(sockaddr_in_cast(ss));
        char buf[INET_ADDRSTRLEN] = {};
        const char* addr = ::inet_ntop(ss->ss_family, &addr4->sin_addr, buf, INET_ADDRSTRLEN);

        if (addr) {
            saddr = addr;
        }

        port = ntohs(addr4->sin_port);
    } else if (ss->ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = const_cast<struct sockaddr_in6*>(sockaddr_in6_cast(ss));
        char buf[INET6_ADDRSTRLEN] = {};
        const char* addr = ::inet_ntop(ss->ss_family, &addr6->sin6_addr, buf, INET6_ADDRSTRLEN);

        if (addr) {
            saddr = addr;
        }

        port = ntohs(addr6->sin6_port);
    } else {
        LOG_ERROR << "unknown socket family connected";
        return empty_string;
    }

    if (!saddr.empty()) {
        saddr.append(":", 1).append(std::to_string(port));
    }

    return saddr;
}

std::string ToIPPort(const struct sockaddr* ss) {
    return ToIPPort(sockaddr_storage_cast(ss));
}

std::string ToIPPort(const struct sockaddr_in* ss) {
    return ToIPPort(sockaddr_storage_cast(ss));
}

std::string ToIP(const struct sockaddr* s) {
    auto ss = sockaddr_storage_cast(s);
    if (ss->ss_family == AF_INET) {
        struct sockaddr_in* addr4 = const_cast<struct sockaddr_in*>(sockaddr_in_cast(ss));
        char buf[INET_ADDRSTRLEN] = {};
        const char* addr = ::inet_ntop(ss->ss_family, &addr4->sin_addr, buf, INET_ADDRSTRLEN);
        if (addr) {
            return std::string(addr);
        }
    } else if (ss->ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = const_cast<struct sockaddr_in6*>(sockaddr_in6_cast(ss));
        char buf[INET6_ADDRSTRLEN] = {};
        const char* addr = ::inet_ntop(ss->ss_family, &addr6->sin6_addr, buf, INET6_ADDRSTRLEN);
        if (addr) {
            return std::string(addr);
        }
    } else {
        LOG_ERROR << "unknown socket family connected";
    }

    return empty_string;
}

void SetTimeout(int fd, uint32_t timeout_ms) {
#ifdef H_OS_WINDOWS
    DWORD tv = timeout_ms;
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#endif
    int ret = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    assert(ret == 0);
    if (ret != 0) {
        int err = errno;
        LOG_ERROR << "setsockopt SO_RCVTIMEO ERROR " << err << strerror(err);
    }
}

void SetTimeout(int fd, const Duration& timeout) {
    SetTimeout(fd, (uint32_t)(timeout.Milliseconds()));
}

void SetKeepAlive(int fd) {
    int on = 1;
    int rc = ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&on, sizeof(on));
    if (rc != 0) {
        int serrno = errno;
        LOG_ERROR << "setsockopt(SO_KEEPALIVE) failed, errno=" << serrno << " " << strerror(serrno);
    }
}

void SetReuseAddr(int fd) {
    int on = 1;
    int rc = ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
    if (rc != 0) {
        int serrno = errno;
        LOG_ERROR << "setsockopt(SO_REUSEADDR) failed, errno=" << serrno << " " << strerror(serrno);
    }
}

void SetReusePort(int fd) {
#ifdef SO_REUSEPORT
    int on = 1;
    int rc = ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char*)&on, sizeof(on));
    if (rc != 0) {
        int serrno = errno;
        LOG_ERROR << "setsockopt(SO_REUSEPORT) failed, errno=" << serrno << " " << strerror(serrno);
    }
#endif
}

}
}

#ifdef H_OS_WINDOWS
int readv(int sockfd, struct iovec* iov, int iovcnt) {
    DWORD readn = 0;
    DWORD flags = 0;

    if (::WSARecv(sockfd, iov, iovcnt, &readn, &flags, NULL, NULL) == 0) {
        return readn;
    }

    return -1;
}
#endif
