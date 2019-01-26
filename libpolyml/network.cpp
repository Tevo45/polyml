/*
    Title:      Network functions.

    Copyright (c) 2000-7, 2016, 2018, 2019 David C. J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License version 2.1 as published by the Free Software Foundation.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#elif defined(_WIN32)
#include "winconfig.h"
#else
#error "No configuration file"
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x) 0
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#if (defined(_WIN32) && ! defined(__CYGWIN__))
#include <winsock2.h>
#else
typedef int SOCKET;
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <limits>
#ifdef max
#undef max
#endif

#include <new>

#include "globals.h"
#include "gc.h"
#include "arb.h"
#include "run_time.h"
#include "mpoly.h"
#include "processes.h"
#include "network.h"
#include "io_internal.h"
#include "sys.h"
#include "polystring.h"
#include "save_vec.h"
#include "rts_module.h"
#include "machine_dep.h"
#include "errors.h"
#include "rtsentry.h"
#include "timing.h"

extern "C" {
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGeneral(PolyObject *threadId, PolyWord code, PolyWord arg);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetServByName(PolyObject *threadId, PolyWord servName);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetServByNameAndProtocol(PolyObject *threadId, PolyWord servName, PolyWord protName);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetServByPort(PolyObject *threadId, PolyWord portNo);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetServByPortAndProtocol(PolyObject *threadId, PolyWord portNo, PolyWord protName);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetProtByName(PolyObject *threadId, PolyWord protocolName);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetProtByNo(PolyObject *threadId, PolyWord protoNo);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetHostName(PolyObject *threadId);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetHostByName(PolyObject *threadId, PolyWord hostName);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetHostByAddr(PolyObject *threadId, PolyWord hostAddr);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkCloseSocket(PolyObject *threadId, PolyWord arg);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkSelect(PolyObject *threadId, PolyWord fdVecTriple, PolyWord maxMillisecs);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkGetSocketError(PolyObject *threadId, PolyWord skt);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkConnect(PolyObject *threadId, PolyWord skt, PolyWord addr);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkAccept(PolyObject *threadId, PolyWord skt);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkSend(PolyObject *threadId, PolyWord args);
    POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkSendTo(PolyObject *threadId, PolyWord args);
}

#define SAVE(x) taskData->saveVec.push(x)
#define ALLOC(n) alloc_and_save(taskData, n)
#define SIZEOF(x) (sizeof(x)/sizeof(PolyWord))

#if (defined(_WIN32) && ! defined(__CYGWIN__))
static int winsock_init = 0; /* Check that it has been initialised. */

#else
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR    (-1)
#endif

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t; // This must be int for Windows at least
#endif

#ifndef SHUT_RD
#define SHUT_RD     0
#endif

#ifndef SHUT_WR
#define SHUT_WR     1
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR   2
#endif

/* Address families.  Although this table is in ascending
   numerical order of address family nothing depends on that.
   The only requirement is that "INET" => AF_INET must always
   be present and "UNIX" => AF_UNIX must be present on Unix.
   Other entries are entirely optional and are for amusement
   only. */
struct af_tab_struct {
    const char *af_name;
    int af_num;
} af_table[] =
{
#ifdef AF_UNIX
    { "UNIX",       AF_UNIX }, /* This is nearly always there. */
#endif
#ifdef AF_LOCAL
    { "LOCAL",      AF_LOCAL },
#endif
    { "INET",       AF_INET }, /* This one should always be there. */
#ifdef AF_IMPLINK
    { "IMPLINK",        AF_IMPLINK },
#endif
#ifdef AF_PUP
    { "PUP",        AF_PUP },
#endif
#ifdef AF_CHAOS
    { "CHAOS",      AF_CHAOS },
#endif
#ifdef AF_IPX
    { "IPX",        AF_IPX },
#endif
#ifdef AF_NS
    { "NS",         AF_NS },
#endif
#ifdef AF_ISO
    { "ISO",        AF_ISO },
#endif
#ifdef AF_OSI
    { "OSI",        AF_OSI },
#endif
#ifdef AF_ECMA
    { "ECMA",       AF_ECMA },
#endif
#ifdef AF_DATAKIT
    { "DATAKIT",        AF_DATAKIT },
#endif
#ifdef AF_CCITT
    { "CCITT",      AF_CCITT },
#endif
#ifdef AF_SNA
    { "SNA",        AF_SNA },
#endif
#ifdef AF_DECnet
    { "DECnet",     AF_DECnet },
#endif
#ifdef AF_DLI
    { "DLI",        AF_DLI },
#endif
#ifdef AF_LAT
    { "LAT",        AF_LAT },
#endif
#ifdef AF_HYLINK
    { "HYLINK",     AF_HYLINK },
#endif
#ifdef AF_APPLETALK
    { "APPLETALK",      AF_APPLETALK },
#endif
#ifdef AF_NETBIOS
    { "NETBIOS",        AF_NETBIOS },
#endif
#ifdef AF_ROUTE
    { "ROUTE",      AF_ROUTE },
#endif
#ifdef AF_VOICEVIEW
    { "VOICEVIEW",      AF_VOICEVIEW },
#endif
#ifdef AF_FIREFOX
    { "FIREFOX",        AF_FIREFOX },
#endif
#ifdef AF_BAN
    { "BAN",        AF_BAN },
#endif
#ifdef AF_LINK
    { "LINK",       AF_LINK },
#endif
#ifdef AF_COIP
    { "COIP",       AF_COIP },
#endif
#ifdef AF_CNT
    { "CNT",        AF_CNT },
#endif
#ifdef AF_SIP
    { "SIP",        AF_SIP },
#endif
#ifdef AF_ISDN
    { "ISDN",       AF_ISDN },
#endif
#ifdef AF_E164
    { "E164",       AF_E164 },
#endif
#ifdef AF_INET6
    { "INET6",      AF_INET6 },
#endif
#ifdef AF_NATM
    { "NATM",       AF_NATM },
#endif
#ifdef AF_ATM
    { "ATM",        AF_ATM },
#endif
#ifdef AF_NETGRAPH
    { "NETGRAPH",       AF_NETGRAPH },
#endif
};

/* Socket types.  Only STREAM and DGRAM are required.  */
struct sk_tab_struct {
    const char *sk_name;
    int sk_num;
} sk_table[] =
{
    { "STREAM",     SOCK_STREAM },
    { "DGRAM",      SOCK_DGRAM },
    { "RAW",        SOCK_RAW },
    { "RDM",        SOCK_RDM },
    { "SEQPACKET",  SOCK_SEQPACKET }
};

static Handle makeHostEntry(TaskData *taskData, struct hostent *host);
static Handle makeProtoEntry(TaskData *taskData, struct protoent *proto);
static Handle mkAftab(TaskData *taskData, void*, char *p);
static Handle mkSktab(TaskData *taskData, void*, char *p);
static Handle setSocketOption(TaskData *taskData, Handle args, int level, int opt);
static Handle getSocketOption(TaskData *taskData, Handle args, int level, int opt);
static Handle getSocketInt(TaskData *taskData, Handle args, int level, int opt);

#if (defined(_WIN32) && ! defined(__CYGWIN__))
#define GETERROR     (WSAGetLastError())
#define TOOMANYFILES    WSAEMFILE
#define NOMEMORY        WSA_NOT_ENOUGH_MEMORY
#define STREAMCLOSED    WSA_INVALID_HANDLE
#define WOULDBLOCK      WSAEWOULDBLOCK
#define INPROGRESS      WSAEINPROGRESS
#define CALLINTERRUPTED WSAEINTR
#undef EBADF
#undef EMFILE
#undef EAGAIN
#undef EINTR
#undef EWOULDBLOCK
#undef ENOMEM
#else
#define GETERROR    (errno)
#define TOOMANYFILES EMFILE
#define NOMEMORY ENOMEM
#define STREAMCLOSED EBADF
#define ERRORNUMBER errno
#define FILEDOESNOTEXIST ENOENT
#define WOULDBLOCK EWOULDBLOCK
#define INPROGRESS EINPROGRESS
#define CALLINTERRUPTED EINTR
#endif


// Wait until "select" returns.  In Windows this is used only for networking.
class WaitSelect: public Waiter
{
public:
    WaitSelect(unsigned maxMillisecs=(unsigned)-1);
    virtual void Wait(unsigned maxMillisecs);
    void SetRead(SOCKET fd) {  FD_SET(fd, &readSet); }
    void SetWrite(SOCKET fd) {  FD_SET(fd, &writeSet); }
    void SetExcept(SOCKET fd)  {  FD_SET(fd, &exceptSet); }
    bool IsSetRead(SOCKET fd) { return FD_ISSET(fd, &readSet) != 0; }
    bool IsSetWrite(SOCKET fd) { return FD_ISSET(fd, &writeSet) != 0; }
    bool IsSetExcept(SOCKET fd) { return FD_ISSET(fd, &exceptSet) != 0; }
    // Save the result of the select call and any associated error
    int SelectResult(void) { return selectResult; }
    int SelectError(void) { return errorResult; }
private:
    fd_set readSet, writeSet, exceptSet;
    int selectResult;
    int errorResult;
    unsigned maxTime;
};

WaitSelect::WaitSelect(unsigned maxMillisecs)
{
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_ZERO(&exceptSet);
    selectResult = 0;
    errorResult = 0;
    maxTime = maxMillisecs;
}

void WaitSelect::Wait(unsigned maxMillisecs)
{
    if (maxTime < maxMillisecs) maxMillisecs = maxTime;
    struct timeval toWait = { 0, 0 };
    toWait.tv_sec = maxMillisecs / 1000;
    toWait.tv_usec = (maxMillisecs % 1000) * 1000;
    selectResult = select(FD_SETSIZE, &readSet, &writeSet, &exceptSet, &toWait);
    if (selectResult < 0) errorResult = GETERROR;
}

class WaitNet: public WaitSelect {
public:
    WaitNet(SOCKET sock, bool isOOB = false);
};

// Use "select" in both Windows and Unix.  In Windows that means we
// don't watch hWakeupEvent but that's only a hint.
WaitNet::WaitNet(SOCKET sock, bool isOOB)
{
    if (isOOB) SetExcept(sock); else SetRead(sock);
}

// Wait for a socket to be free to write.
class WaitNetSend: public WaitSelect {
public:
    WaitNetSend(SOCKET sock) { SetWrite(sock); }
};

#if (defined(_WIN32) && ! defined(__CYGWIN__))
class WinSocket : public WinStreamBase
{
public:
    WinSocket(SOCKET skt) : socket(skt) {}

    virtual SOCKET getSocket() {
        return socket;
    }

    virtual int pollTest() {
        // We can poll for any of these.
        return POLL_BIT_IN | POLL_BIT_OUT | POLL_BIT_PRI;
    }

    virtual int poll(TaskData *taskData, int test);

public:
    SOCKET socket;
};

// Poll without blocking.
int WinSocket::poll(TaskData *taskData, int bits)
{
    int result = 0;
    if (bits & POLL_BIT_PRI)
    {
        u_long atMark = 0;
        if (ioctlsocket(socket, SIOCATMARK, &atMark) != 0)
            raise_syscall(taskData, "ioctlsocket failed", GETERROR);
        if (atMark) { result |= POLL_BIT_PRI; }
    }
    if (bits & (POLL_BIT_IN | POLL_BIT_OUT))
    {
        FD_SET readFds, writeFds;
        TIMEVAL poll = { 0, 0 };
        FD_ZERO(&readFds); FD_ZERO(&writeFds);
        if (bits & POLL_BIT_IN) FD_SET(socket, &readFds);
        if (bits & POLL_BIT_OUT) FD_SET(socket, &writeFds);
        int selRes = select(FD_SETSIZE, &readFds, &writeFds, NULL, &poll);
        if (selRes < 0)
            raise_syscall(taskData, "select failed", GETERROR);
        else if (selRes > 0)
        {
            // N.B. select only tells us about out-of-band data if SO_OOBINLINE is FALSE. */
            if (FD_ISSET(socket, &readFds)) result |= POLL_BIT_IN;
            if (FD_ISSET(socket, &writeFds)) result |= POLL_BIT_OUT;
        }
    }
    return result;
}

static SOCKET getStreamSocket(TaskData *taskData, PolyWord strm)
{
    WinSocket *winskt = *(WinSocket**)(strm.AsObjPtr());
    if (winskt == 0)
        raise_syscall(taskData, "Stream is closed", STREAMCLOSED);
    return winskt->getSocket();
}

static Handle wrapStreamSocket(TaskData *taskData, SOCKET skt)
{
    try {
        WinSocket *winskt = new WinSocket(skt);
        return MakeVolatileWord(taskData, winskt);
    }
    catch (std::bad_alloc&) {
        raise_syscall(taskData, "Insufficient memory", NOMEMORY);
    }
}

#else

static SOCKET getStreamSocket(TaskData *taskData, PolyWord strm)
{
    return getStreamFileDescriptor(taskData, strm);
}

static Handle wrapStreamSocket(TaskData *taskData, SOCKET skt)
{
    return wrapFileDescriptor(taskData, skt);
}
#endif

static Handle Net_dispatch_c(TaskData *taskData, Handle args, Handle code)
{
    unsigned c = get_C_unsigned(taskData, code->Word());
    Handle hSave = taskData->saveVec.mark();
TryAgain: // Used for various retries.
          // N.B.  If we call ThreadPause etc we may GC.  We MUST reload any handles so for
          // safety we always come back here.
    switch (c)
    {

    case 11:
        {
            /* Return a list of known address families. */
            return makeList(taskData, sizeof(af_table)/sizeof(af_table[0]),
                            (char*)af_table, sizeof(af_table[0]),
                            0, mkAftab);
        }

    case 12:
        {
            /* Return a list of known socket types. */
            return makeList(taskData, sizeof(sk_table)/sizeof(sk_table[0]),
                            (char*)sk_table, sizeof(sk_table[0]),
                            0, mkSktab);
        }

    case 13: /* Return the "any" internet address. */
        return Make_arbitrary_precision(taskData, INADDR_ANY);

    case 14: /* Create a socket */
        {
            int af = get_C_int(taskData, DEREFHANDLE(args)->Get(0));
            int type = get_C_int(taskData, DEREFHANDLE(args)->Get(1));
            int proto = get_C_int(taskData, DEREFHANDLE(args)->Get(2));
            SOCKET skt = socket(af, type, proto);
            if (skt == INVALID_SOCKET)
            {
                switch (GETERROR)
                {
                case CALLINTERRUPTED:
                    taskData->saveVec.reset(hSave);
                    goto TryAgain;
                default: raise_syscall(taskData, "socket failed", GETERROR);
                }
            }
            /* Set the socket to non-blocking mode. */
#if (defined(_WIN32) && ! defined(__CYGWIN__))
            unsigned long onOff = 1;
            if (ioctlsocket(skt, FIONBIO, &onOff) != 0)
#else
            int onOff = 1;
            if (ioctl(skt, FIONBIO, &onOff) < 0)
#endif
            {
#if (defined(_WIN32) && ! defined(__CYGWIN__))
                closesocket(skt);
#else
                close(skt);
#endif
                raise_syscall(taskData, "ioctl failed", GETERROR);
            }
            return wrapStreamSocket(taskData, skt);
        }

    case 15: /* Set TCP No-delay option. */
        return setSocketOption(taskData, args, IPPROTO_TCP, TCP_NODELAY);

    case 16: /* Get TCP No-delay option. */
        return getSocketOption(taskData, args, IPPROTO_TCP, TCP_NODELAY);

    case 17: /* Set Debug option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_DEBUG);

    case 18: /* Get Debug option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_DEBUG);

    case 19: /* Set REUSEADDR option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_REUSEADDR);

    case 20: /* Get REUSEADDR option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_REUSEADDR);

    case 21: /* Set KEEPALIVE option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_KEEPALIVE);

    case 22: /* Get KEEPALIVE option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_KEEPALIVE);

    case 23: /* Set DONTROUTE option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_DONTROUTE);

    case 24: /* Get DONTROUTE option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_DONTROUTE);

    case 25: /* Set BROADCAST option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_BROADCAST);

    case 26: /* Get BROADCAST option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_BROADCAST);

    case 27: /* Set OOBINLINE option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_OOBINLINE);

    case 28: /* Get OOBINLINE option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_OOBINLINE);

    case 29: /* Set SNDBUF size. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_SNDBUF);

    case 30: /* Get SNDBUF size. */
        return getSocketInt(taskData, args, SOL_SOCKET, SO_SNDBUF);

    case 31: /* Set RCVBUF size. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_RCVBUF);

    case 32: /* Get RCVBUF size. */
        return getSocketInt(taskData, args, SOL_SOCKET, SO_RCVBUF);

    case 33: /* Get socket type e.g. SOCK_STREAM. */
        return getSocketInt(taskData, args, SOL_SOCKET, SO_TYPE);

    case 34: /* Get error status and clear it. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_ERROR);

    case 35: /* Set Linger time. */
        {
            struct linger linger;
            SOCKET skt = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
            int lTime = get_C_int(taskData, DEREFHANDLE(args)->Get(1));
            /* We pass in a negative value to turn the option off,
               zero or positive to turn it on. */
            if (lTime < 0)
            {
                linger.l_onoff = 0;
                linger.l_linger = 0;
            }
            else
            {
                linger.l_onoff = 1;
                linger.l_linger = lTime;
            }
            if (setsockopt(skt, SOL_SOCKET, SO_LINGER,
                (char*)&linger, sizeof(linger)) != 0)
                raise_syscall(taskData, "setsockopt failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 36: /* Get Linger time. */
        {
            struct linger linger;
            SOCKET skt = getStreamSocket(taskData, args->Word());
            socklen_t size = sizeof(linger);
            int lTime = 0;
            if (getsockopt(skt, SOL_SOCKET, SO_LINGER,
                (char*)&linger, &size) != 0)
                raise_syscall(taskData, "getsockopt failed", GETERROR);
            /* If the option is off return a negative. */
            if (linger.l_onoff == 0) lTime = -1;
            else lTime = linger.l_linger;
            return Make_arbitrary_precision(taskData, lTime);
        }

    case 37: /* Get peer name. */
        {
            SOCKET skt = getStreamSocket(taskData, args->Word());
            struct sockaddr_storage sockA;
            socklen_t size = sizeof(sockA);
            if (getpeername(skt, (struct sockaddr*)&sockA, &size) != 0)
                raise_syscall(taskData, "getpeername failed", GETERROR);
            if (size > sizeof(sockA)) size = sizeof(sockA);
            /* Addresses are treated as strings. */
            return(SAVE(C_string_to_Poly(taskData, (char*)&sockA, size)));
        }

    case 38: /* Get socket name. */
        {
            SOCKET skt = getStreamSocket(taskData, args->Word());
            struct sockaddr_storage sockA;
            socklen_t   size = sizeof(sockA);
            if (getsockname(skt, (struct sockaddr*)&sockA, &size) != 0)
                raise_syscall(taskData, "getsockname failed", GETERROR);
            if (size > sizeof(sockA)) size = sizeof(sockA);
            return(SAVE(C_string_to_Poly(taskData, (char*)&sockA, size)));
        }

    case 39: /* Return the address family from an address. */
        {
            PolyStringObject *psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr *psock = (struct sockaddr *)&psAddr->chars;
            return Make_arbitrary_precision(taskData, psock->sa_family);
        }

    case 40: /* Create a socket address from a port number and
                internet address. */
        {
            struct sockaddr_in sockaddr;
            memset(&sockaddr, 0, sizeof(sockaddr));
            sockaddr.sin_family = AF_INET;
            sockaddr.sin_port = htons(get_C_ushort(taskData, DEREFHANDLE(args)->Get(0)));
            sockaddr.sin_addr.s_addr =
                htonl(get_C_unsigned(taskData, DEREFHANDLE(args)->Get(1)));
            return(SAVE(C_string_to_Poly(taskData, (char*)&sockaddr, sizeof(sockaddr))));
        }

    case 41: /* Return port number from an internet socket address.
                Assumes that we've already checked the address family. */
        {
            PolyStringObject *psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr_in *psock =
                (struct sockaddr_in *)&psAddr->chars;
            return Make_arbitrary_precision(taskData, ntohs(psock->sin_port));
        }

    case 42: /* Return internet address from an internet socket address.
                Assumes that we've already checked the address family. */
        {
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr_in *psock =
                (struct sockaddr_in *)&psAddr->chars;
            return Make_arbitrary_precision(taskData, ntohl(psock->sin_addr.s_addr));
        }

        /* 43 - Set non-blocking mode.  Now removed. */

    case 44: /* Find number of bytes available. */
        {
            SOCKET skt = getStreamSocket(taskData, args->Word());
#if (defined(_WIN32) && ! defined(__CYGWIN__))
            unsigned long readable;
            if (ioctlsocket(skt, FIONREAD, &readable) != 0)
                raise_syscall(taskData, "ioctlsocket failed", GETERROR);
#else
            int readable;
            if (ioctl(skt, FIONREAD, &readable) < 0)
                raise_syscall(taskData, "ioctl failed", GETERROR);
#endif
            return Make_arbitrary_precision(taskData, readable);
        }

    case 45: /* Find out if we are at the mark. */
        {
            SOCKET skt = getStreamSocket(taskData, args->Word());
#if (defined(_WIN32) && ! defined(__CYGWIN__))
            unsigned long atMark;
            if (ioctlsocket(skt, SIOCATMARK, &atMark) != 0)
                raise_syscall(taskData, "ioctlsocket failed", GETERROR);
#else
            int atMark;
            if (ioctl(skt, SIOCATMARK, &atMark) < 0)
                raise_syscall(taskData, "ioctl failed", GETERROR);
#endif
            return Make_arbitrary_precision(taskData, atMark == 0 ? 0 : 1);
        }

    case 47: /* Bind an address to a socket. */
        {
            SOCKET skt = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP()->Get(1).AsObjPtr();
            struct sockaddr *psock = (struct sockaddr *)&psAddr->chars;
            if (bind(skt, psock, (int)psAddr->length) != 0)
                raise_syscall(taskData, "bind failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 49: /* Put socket into listening mode. */
        {
            SOCKET sock = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
            int backlog = get_C_int(taskData, DEREFHANDLE(args)->Get(1));
            if (listen(sock, backlog) != 0)
                raise_syscall(taskData, "listen failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 50: /* Shutdown the socket. */
        {
            SOCKET sock = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
            int mode = 0;
            switch (get_C_ulong(taskData, DEREFHANDLE(args)->Get(1)))
            {
            case 1: mode = SHUT_RD; break;
            case 2: mode = SHUT_WR; break;
            case 3: mode = SHUT_RDWR;
            }
            if (shutdown(sock, mode) != 0)
                raise_syscall(taskData, "shutdown failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 53: /* Receive data into an array. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);
    case 62: /* Non-blocking receive. */
        {
            SOCKET sock = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
            char *base = (char*)DEREFHANDLE(args)->Get(1).AsObjPtr()->AsBytePtr();
            POLYUNSIGNED offset = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(2));
#if(defined(_WIN32) && ! defined(_CYGWIN))
            int length = get_C_int(taskData, DEREFHANDLE(args)->Get(3));
#else
            size_t length = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(3));
#endif
            unsigned int peek = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(4));
            unsigned int outOfBand = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(5));
            int flags = 0;
            if (peek != 0) flags |= MSG_PEEK;
            if (outOfBand != 0) flags |= MSG_OOB;

            while (1) {
                int err;
#if(defined(_WIN32) && ! defined(_CYGWIN))
                int recvd;
#else
                ssize_t recvd;
#endif
                recvd = recv(sock, base+offset, length, flags);
                err = GETERROR;
                if (recvd != SOCKET_ERROR) { /* OK. */
                    /* It appears that recv may return the length of the
                       message if that is longer than the buffer. */
                    if (recvd > (int)length) recvd = length;
                    return Make_arbitrary_precision(taskData, recvd);
                }
                if ((err == WOULDBLOCK || err == INPROGRESS) && c == 53 /* blocking */)
                {
                    /* Block until something arrives. */
                    WaitNet waiter(sock, outOfBand != 0);
                    processes->ThreadPauseForIO(taskData, &waiter);
                    // It is NOT safe to just loop here.  We may have GCed.
                    taskData->saveVec.reset(hSave);
                    goto TryAgain;
                }
                else if (err != CALLINTERRUPTED)
                    raise_syscall(taskData, "recv failed", err);
                /* else try again */
            }
        }

    case 54: /* Receive data into an array and return the sender's
                address along with the length.  In Windows this can
                only be used with datagrams. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);
    case 63: /* Non-blocking receive. */
        {
            SOCKET sock = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
            char *base = (char*)DEREFHANDLE(args)->Get(1).AsObjPtr()->AsBytePtr();
            POLYUNSIGNED offset = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(2));
#if(defined(_WIN32) && ! defined(_CYGWIN))
            int length = get_C_int(taskData, DEREFHANDLE(args)->Get(3));
#else
            size_t length = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(3));
#endif
            unsigned int peek = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(4));
            unsigned int outOfBand = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(5));
            int flags = 0;
            struct sockaddr_storage resultAddr;
            socklen_t addrLen = sizeof(resultAddr);

            if (peek != 0) flags |= MSG_PEEK;
            if (outOfBand != 0) flags |= MSG_OOB;

            while (1) {
                int err;
#if(defined(_WIN32) && ! defined(_CYGWIN))
                int recvd;
#else
                ssize_t recvd;
#endif
                recvd = recvfrom(sock, base+offset, length, flags, (struct sockaddr*)&resultAddr, &addrLen);
                err = GETERROR;

                if (recvd != SOCKET_ERROR) { /* OK. */
                    Handle addrHandle, lengthHandle, pair;
                    if (recvd > (int)length) recvd = length;
                    lengthHandle = Make_arbitrary_precision(taskData, recvd);
                    if (addrLen > sizeof(resultAddr)) addrLen = sizeof(resultAddr);
                    addrHandle = SAVE(C_string_to_Poly(taskData, (char*)&resultAddr, addrLen));
                    pair = ALLOC(2);
                    DEREFHANDLE(pair)->Set(0, lengthHandle->Word());
                    DEREFHANDLE(pair)->Set(1, addrHandle->Word());
                    return pair;
                }
                if ((err == WOULDBLOCK || err == INPROGRESS) && c == 54 /* blocking */)
                {
                    WaitNet waiter(sock, outOfBand != 0);
                    processes->ThreadPauseForIO(taskData, &waiter);
                    // It is NOT safe to just loop here.  We may have GCed.
                    taskData->saveVec.reset(hSave);
                    goto TryAgain;
                }
                else if (err != CALLINTERRUPTED)
                    raise_syscall(taskData, "recvfrom failed", err);
                /* else try again */
            }
        }

    case 55: /* Create a socket pair. */
#if (defined(_WIN32) && ! defined(__CYGWIN__))
        /* Not implemented. */
        raise_syscall(taskData, "socketpair not implemented", WSAEAFNOSUPPORT);
#else
        {
            Handle pair;
            
            int af = get_C_long(taskData, DEREFHANDLE(args)->Get(0));
            int type = get_C_long(taskData, DEREFHANDLE(args)->Get(1));
            int proto = get_C_long(taskData, DEREFHANDLE(args)->Get(2));
            int onOff = 1;
            SOCKET skt[2];
            if (socketpair(af, type, proto, skt) != 0)
            {
                switch (GETERROR)
                {
                case CALLINTERRUPTED:
                    taskData->saveVec.reset(hSave);
                    goto TryAgain;
                default: raise_syscall(taskData, "socketpair failed", GETERROR);
                }
            }
            /* Set the sockets to non-blocking mode. */
            if (ioctl(skt[0], FIONBIO, &onOff) < 0 ||
                ioctl(skt[1], FIONBIO, &onOff) < 0)
            {
                close(skt[0]);
                close(skt[1]);
                raise_syscall(taskData, "ioctl failed", GETERROR);
            }
            Handle str_token1 = wrapStreamSocket(taskData, skt[0]);
            Handle str_token2 = wrapStreamSocket(taskData, skt[1]);
            /* Return the two streams as a pair. */
            pair = ALLOC(2);
            DEREFHANDLE(pair)->Set(0, DEREFWORD(str_token1));
            DEREFHANDLE(pair)->Set(1, DEREFWORD(str_token2));
            return pair;
        }
#endif

    case 56: /* Create a Unix socket address from a string. */
#if (defined(_WIN32) && ! defined(__CYGWIN__))
        /* Not implemented. */
        raise_syscall(taskData, "Unix addresses not implemented", WSAEAFNOSUPPORT);
#else
        {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
            addr.sun_len = sizeof(addr); // Used in FreeBSD only.
#endif
            POLYUNSIGNED length = Poly_string_to_C(DEREFWORD(args), addr.sun_path, sizeof(addr.sun_path));
            if (length > (int)sizeof(addr.sun_path))
                raise_syscall(taskData, "Address too long", ENAMETOOLONG);
            return SAVE(C_string_to_Poly(taskData, (char*)&addr, sizeof(addr)));
        }
#endif

    case 57: /* Get the file name from a Unix socket address. */
#if (defined(_WIN32) && ! defined(__CYGWIN__))
        /* Not implemented. */
        raise_syscall(taskData, "Unix addresses not implemented", WSAEAFNOSUPPORT);
#else
        {
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr_un *psock = (struct sockaddr_un *)&psAddr->chars;
            return SAVE(C_string_to_Poly(taskData, psock->sun_path));
        }
#endif

    default:
        {
            char msg[100];
            sprintf(msg, "Unknown net function: %d", c);
            raise_exception_string(taskData, EXC_Fail, msg);
            return 0;
        }
    }
}

static Handle mkAddr(TaskData *taskData, void *arg, char *p)
{
    int j;
    struct hostent *host = (struct hostent *)arg;
    unsigned long addr = 0;
    /* Addresses are in network order so this is fairly easy.
       In practice they will be 4 byte entries so we could
       just use ntohl. */
    for (j = 0; j < host->h_length; j++)
        addr = (addr << 8) | ((*(char**)p)[j] & 255);
    return Make_arbitrary_precision(taskData, addr);
}

/* Convert a host entry into a tuple for ML. */
static Handle makeHostEntry(TaskData *taskData, struct hostent *host)
{
    /* We need to do all this in the right order.  We cannot
       construct the result tuple until all the values are
       ready.  We have to save each entry on the save stack
       just in case of a garbage collection. */
    int i;
    char **p;
    Handle aliases, name, addrType, result;
    Handle addrList = SAVE(ListNull);

    /* Canonical name. */
    name = SAVE(C_string_to_Poly(taskData, host->h_name));

    /* Aliases. */
    for (i=0, p = host->h_aliases; *p != NULL; p++, i++);
    aliases = convert_string_list(taskData, i, host->h_aliases);

    /* Address type. */
    addrType = Make_arbitrary_precision(taskData, host->h_addrtype);

    /* Addresses. */
    /* Count them first and then work from the end back. */
    for (i=0, p = host->h_addr_list; *p != NULL; p++, i++);
    addrList = makeList(taskData, i, (char*)host->h_addr_list, sizeof(char*), host, mkAddr);

    /* Make the result structure. */
    result = ALLOC(4);
    DEREFHANDLE(result)->Set(0, name->Word());
    DEREFHANDLE(result)->Set(1, aliases->Word());
    DEREFHANDLE(result)->Set(2, addrType->Word());
    DEREFHANDLE(result)->Set(3, addrList->Word());
    return result;
}

static Handle makeProtoEntry(TaskData *taskData, struct protoent *proto)
{
    int i;
    char **p;
    Handle aliases, name, protocol, result;

    /* Canonical name. */
    name = SAVE(C_string_to_Poly(taskData, proto->p_name));

    /* Aliases. */
    for (i=0, p = proto->p_aliases; *p != NULL; p++, i++);
    aliases = convert_string_list(taskData, i, proto->p_aliases);

    /* Protocol number. */
    protocol = Make_arbitrary_precision(taskData, proto->p_proto);

    /* Make the result structure. */
    result = ALLOC(3);
    DEREFHANDLE(result)->Set(0, name->Word());
    DEREFHANDLE(result)->Set(1, aliases->Word());
    DEREFHANDLE(result)->Set(2, protocol->Word());
    return result;
}

static Handle makeServEntry(TaskData *taskData, struct servent *serv)
{
    int i;
    char **p;
    Handle aliases, name, protocol, result, port;

    /* Canonical name. */
    name = SAVE(C_string_to_Poly(taskData, serv->s_name));

    /* Aliases. */
    for (i=0, p = serv->s_aliases; *p != NULL; p++, i++);
    aliases = convert_string_list(taskData, i, serv->s_aliases);

    /* Port number. */
    port = Make_arbitrary_precision(taskData, ntohs(serv->s_port));

    /* Protocol name. */
    protocol = SAVE(C_string_to_Poly(taskData, serv->s_proto));

    /* Make the result structure. */
    result = ALLOC(4);
    DEREFHANDLE(result)->Set(0, name->Word());
    DEREFHANDLE(result)->Set(1, aliases->Word());
    DEREFHANDLE(result)->Set(2, port->Word());
    DEREFHANDLE(result)->Set(3, protocol->Word());
    return result;
}

static Handle mkAftab(TaskData *taskData, void *arg, char *p)
{
    struct af_tab_struct *af = (struct af_tab_struct *)p;
    Handle result, name, num;
    /* Construct a pair of the string and the number. */
    name = SAVE(C_string_to_Poly(taskData, af->af_name));
    num = Make_arbitrary_precision(taskData, af->af_num);
    result = ALLOC(2);
    DEREFHANDLE(result)->Set(0, name->Word());
    DEREFHANDLE(result)->Set(1, num->Word());
    return result;
}

static Handle mkSktab(TaskData *taskData, void *arg, char *p)
{
    struct sk_tab_struct *sk = (struct sk_tab_struct *)p;
    Handle result, name, num;
    /* Construct a pair of the string and the number. */
    name = SAVE(C_string_to_Poly(taskData, sk->sk_name));
    num = Make_arbitrary_precision(taskData, sk->sk_num);
    result = ALLOC(2);
    DEREFHANDLE(result)->Set(0, name->Word());
    DEREFHANDLE(result)->Set(1, num->Word());
    return result;
}

/* This sets an option and can also be used to set an integer. */
static Handle setSocketOption(TaskData *taskData, Handle args, int level, int opt)
{
    SOCKET sock = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
    int onOff = get_C_int(taskData, DEREFHANDLE(args)->Get(1));
    if (setsockopt(sock, level, opt,
        (char*)&onOff, sizeof(int)) != 0)
        raise_syscall(taskData, "setsockopt failed", GETERROR);
    return Make_arbitrary_precision(taskData, 0);
}

/* Get a socket option as a boolean */
static Handle getSocketOption(TaskData *taskData, Handle args, int level, int opt)
{
    SOCKET sock = getStreamSocket(taskData, args->Word());
    int onOff = 0;
    socklen_t size = sizeof(int);
    if (getsockopt(sock, level, opt, (char*)&onOff, &size) != 0)
        raise_syscall(taskData, "getsockopt failed", GETERROR);
    return Make_arbitrary_precision(taskData, onOff == 0 ? 0 : 1);
}

/* Get a socket option as an integer */
static Handle getSocketInt(TaskData *taskData, Handle args, int level, int opt)
{
    SOCKET sock = getStreamSocket(taskData, args->Word());
    int optVal = 0;
    socklen_t size = sizeof(int);
    if (getsockopt(sock, level, opt, (char*)&optVal, &size) != 0)
        raise_syscall(taskData, "getsockopt failed", GETERROR);
    return Make_arbitrary_precision(taskData, optVal);
}

POLYUNSIGNED PolyNetworkGetSocketError(PolyObject *threadId, PolyWord skt)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle result = 0;

    try {
        SOCKET sock = getStreamSocket(taskData, skt);
        int intVal = 0;
        socklen_t size = sizeof(int);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&intVal, &size) != 0)
            raise_syscall(taskData, "getsockopt failed", GETERROR);
        result = Make_sysword(taskData, intVal);
    }
    catch (...) {} // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

// Helper function for selectCall.  Creates the result vector of active sockets.
static bool testBit(int offset, SOCKET fd, WaitSelect *pSelect)
{
    switch (offset)
    {
    case 0: return pSelect->IsSetRead(fd);
    case 1: return pSelect->IsSetWrite(fd);
    case 2: return pSelect->IsSetExcept(fd);
    default: return false;
    }
}

static Handle getSelectResult(TaskData *taskData, Handle args, int offset, WaitSelect *pSelect)
{
    /* Construct the result vectors. */
    PolyObject *inVec = DEREFHANDLE(args)->Get(offset).AsObjPtr();
    POLYUNSIGNED nVec = inVec->Length();
    int nRes = 0;
    POLYUNSIGNED i;
    for (i = 0; i < nVec; i++) {
        SOCKET sock = getStreamSocket(taskData, inVec->Get(i));
        if (testBit(offset, sock, pSelect)) nRes++;
    }
    if (nRes == 0)
        return ALLOC(0); /* None - return empty vector. */
    else {
        Handle result = ALLOC(nRes);
        inVec = DEREFHANDLE(args)->Get(offset).AsObjPtr(); /* It could have moved as a result of a gc. */
        nRes = 0;
        for (i = 0; i < nVec; i++) {
            SOCKET sock = getStreamSocket(taskData, inVec->Get(i));
            if (testBit(offset, sock, pSelect))
                DEREFWORDHANDLE(result)->Set(nRes++, inVec->Get(i));
        }
        return result;
    }
}

/* Wrapper for "select" call.  The arguments are arrays of socket ids.  These arrays are
   updated so that "active" sockets are left unchanged and inactive sockets are set to
   minus one.  */
POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkSelect(PolyObject *threadId, PolyWord fdVecTriple, PolyWord maxMillisecs)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle result = 0;
    POLYUNSIGNED maxMilliseconds = maxMillisecs.UnTaggedUnsigned();
    Handle fdVecTripleHandle = taskData->saveVec.push(fdVecTriple);
    /* Set up the bitmaps for the select call from the arrays. */

    try {
        WaitSelect waitSelect(maxMilliseconds);
        PolyObject *readVec = fdVecTripleHandle->WordP()->Get(0).AsObjPtr();
        PolyObject *writeVec = fdVecTripleHandle->WordP()->Get(1).AsObjPtr();
        PolyObject *excVec = fdVecTripleHandle->WordP()->Get(2).AsObjPtr();
        for (POLYUNSIGNED i = 0; i < readVec->Length(); i++)
            waitSelect.SetRead(getStreamSocket(taskData, readVec->Get(i)));
        for (POLYUNSIGNED i = 0; i < writeVec->Length(); i++)
            waitSelect.SetWrite(getStreamSocket(taskData, writeVec->Get(i)));
        for (POLYUNSIGNED i = 0; i < excVec->Length(); i++)
            waitSelect.SetExcept(getStreamSocket(taskData, excVec->Get(i)));

        // Do the select.  This may return immediately if the maximum time-out is short.
        processes->ThreadPauseForIO(taskData, &waitSelect);
        if (waitSelect.SelectResult() < 0)
            raise_syscall(taskData, "select failed", waitSelect.SelectError());

        // Construct the result vectors.
        Handle rdResult = getSelectResult(taskData, fdVecTripleHandle, 0, &waitSelect);
        Handle wrResult = getSelectResult(taskData, fdVecTripleHandle, 1, &waitSelect);
        Handle exResult = getSelectResult(taskData, fdVecTripleHandle, 2, &waitSelect);
        result = ALLOC(3);
        DEREFHANDLE(result)->Set(0, rdResult->Word());
        DEREFHANDLE(result)->Set(1, wrResult->Word());
        DEREFHANDLE(result)->Set(2, exResult->Word());
    }
    catch (...) {} // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();

}

POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkConnect(PolyObject *threadId, PolyWord skt, PolyWord addr)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    try {
        SOCKET sock = getStreamSocket(taskData, skt);
        PolyStringObject * psAddr = (PolyStringObject *)(addr.AsObjPtr());
        struct sockaddr *psock = (struct sockaddr *)&psAddr->chars;
        // Begin the connection.  The socket is always non-blocking so this will return immediately.
        if (connect(sock, psock, (int)psAddr->length) != 0)
            raise_syscall(taskData, "connect failed", GETERROR);
    }
    catch (...) {} // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    return TAGGED(0).AsUnsigned(); // Always returns unit
}

POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkAccept(PolyObject *threadId, PolyWord skt)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle result = 0;

    try {
        SOCKET sock = getStreamSocket(taskData, skt);
        struct sockaddr_storage resultAddr;
        socklen_t addrLen = sizeof(resultAddr);
        SOCKET resultSkt = accept(sock, (struct sockaddr*)&resultAddr, &addrLen);
        if (resultSkt == INVALID_SOCKET)
            raise_syscall(taskData, "accept failed", GETERROR);
        if (addrLen > sizeof(resultAddr)) addrLen = sizeof(resultAddr);
        Handle addrHandle = taskData->saveVec.push(C_string_to_Poly(taskData, (char*)&resultAddr, addrLen));
        // Return a pair of the new socket and the address.
        Handle resSkt = wrapStreamSocket(taskData, resultSkt);
        result = alloc_and_save(taskData, 2);
        result->WordP()->Set(0, resSkt->Word());
        result->WordP()->Set(1, addrHandle->Word());
    }
    catch (...) {} // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkSend(PolyObject *threadId, PolyWord argsAsWord)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle args = taskData->saveVec.push(argsAsWord);
#if(defined(_WIN32) && ! defined(_CYGWIN))
    int sent = 0;
#else
    ssize_t sent = 0;
#endif

    try {
        SOCKET sock = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
        PolyWord pBase = DEREFHANDLE(args)->Get(1);
        POLYUNSIGNED offset = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(2));
#if(defined(_WIN32) && ! defined(_CYGWIN))
        int length = get_C_int(taskData, DEREFHANDLE(args)->Get(3));
#else
        ssize_t length = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(3));
#endif
        unsigned int dontRoute = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(4));
        unsigned int outOfBand = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(5));
        int flags = 0;
        if (dontRoute != 0) flags |= MSG_DONTROUTE;
        if (outOfBand != 0) flags |= MSG_OOB;
        char *base = (char*)pBase.AsObjPtr()->AsBytePtr();
        sent = send(sock, base + offset, length, flags);
        if (sent == SOCKET_ERROR)
            raise_syscall(taskData, "send failed", GETERROR);
    }
    catch (...) {} // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    return TAGGED(sent).AsUnsigned();
}

POLYEXTERNALSYMBOL POLYUNSIGNED PolyNetworkSendTo(PolyObject *threadId, PolyWord argsAsWord)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle args = taskData->saveVec.push(argsAsWord);
#if(defined(_WIN32) && ! defined(_CYGWIN))
    int sent = 0;
#else
    ssize_t sent = 0;
#endif

    try {
        SOCKET sock = getStreamSocket(taskData, DEREFHANDLE(args)->Get(0));
        PolyStringObject * psAddr = (PolyStringObject *)args->WordP()->Get(1).AsObjPtr();
        PolyWord pBase = DEREFHANDLE(args)->Get(2);

        POLYUNSIGNED offset = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(3));
#if(defined(_WIN32) && ! defined(_CYGWIN))
        int length = get_C_int(taskData, DEREFHANDLE(args)->Get(4));
#else
        size_t length = getPolyUnsigned(taskData, DEREFHANDLE(args)->Get(4));
#endif
        unsigned int dontRoute = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(5));
        unsigned int outOfBand = get_C_unsigned(taskData, DEREFHANDLE(args)->Get(6));
        int flags = 0;
        if (dontRoute != 0) flags |= MSG_DONTROUTE;
        if (outOfBand != 0) flags |= MSG_OOB;
        char *base = (char*)pBase.AsObjPtr()->AsBytePtr();
        sent = sendto(sock, base + offset, length, flags,
                (struct sockaddr *)psAddr->chars, (int)psAddr->length);
        if (sent == SOCKET_ERROR)
            raise_syscall(taskData, "sendto failed", GETERROR);
    }
    catch (...) {} // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    return TAGGED(sent).AsUnsigned();
}

// General interface to networking.  Ideally the various cases will be made into
// separate functions.
POLYUNSIGNED PolyNetworkGeneral(PolyObject *threadId, PolyWord code, PolyWord arg)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle pushedCode = taskData->saveVec.push(code);
    Handle pushedArg = taskData->saveVec.push(arg);
    Handle result = 0;

    try {
        result = Net_dispatch_c(taskData, pushedArg, pushedCode);
    }
    catch (KillException &) {
        processes->ThreadExit(taskData); // May test for kill
    }
    catch (...) { } // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetServByName(PolyObject *threadId, PolyWord serviceName)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Get service given service name only. */
    TempCString servName(Poly_string_to_C_alloc(serviceName));
    struct servent *serv = getservbyname (servName, NULL);
    // If this fails the ML function returns NONE
    Handle result = serv == NULL ? 0 : makeServEntry(taskData, serv);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetServByNameAndProtocol(PolyObject *threadId, PolyWord serviceName, PolyWord protName)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Get service given service name and protocol name. */
    TempCString servName(Poly_string_to_C_alloc(serviceName));
    TempCString protoName(Poly_string_to_C_alloc(protName));
    struct servent *serv = getservbyname (servName, protoName);
    Handle result = serv == NULL ? 0 : makeServEntry(taskData, serv);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetServByPort(PolyObject *threadId, PolyWord portNo)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Get service given port number only. */
    long port = htons(get_C_ushort(taskData, portNo));
    struct servent *serv = getservbyport(port, NULL);
    Handle result = serv == NULL ? 0 : makeServEntry(taskData, serv);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetServByPortAndProtocol(PolyObject *threadId, PolyWord portNo, PolyWord protName)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Get service given port number and protocol name. */
    long port = htons(get_C_ushort(taskData, portNo));
    TempCString protoName(Poly_string_to_C_alloc(protName));
    struct servent *serv = getservbyport (port, protoName);
    Handle result = serv == NULL ? 0 : makeServEntry(taskData, serv);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetProtByName(PolyObject *threadId, PolyWord protocolName)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Look up protocol entry. */
    TempCString protoName(Poly_string_to_C_alloc(protocolName));
    struct protoent *proto = getprotobyname(protoName);
    // If this fails the ML function returns NONE
    Handle result = proto == NULL ? 0 : makeProtoEntry(taskData, proto);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetProtByNo(PolyObject *threadId, PolyWord protoNo)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Look up protocol entry. */
    int pNum = get_C_int(taskData, protoNo);
    struct protoent *proto = getprotobynumber(pNum);
    Handle result = proto == NULL ? 0 : makeProtoEntry(taskData, proto);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetHostName(PolyObject *threadId)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle result = 0;

    try { /* Get the current host name. */
        size_t size = 4096;
        TempCString hostName((char *)malloc(size));
        if (hostName == NULL) raise_syscall(taskData, "Insufficient memory", NOMEMORY);
        int err;
        while ((err = gethostname(hostName, size)) != 0 && GETERROR == ENAMETOOLONG)
        {
            if (size > std::numeric_limits<size_t>::max() / 2) raise_fail(taskData, "gethostname needs too large a buffer");
            size *= 2;
            char *new_buf = (char *)realloc(hostName, size);
            if (new_buf == NULL) raise_syscall(taskData, "Insufficient memory", NOMEMORY);
            hostName = new_buf;
        }

        if (err != 0)
            raise_syscall(taskData, "gethostname failed", GETERROR);

        result = SAVE(C_string_to_Poly(taskData, hostName));
    }
    catch (...) { } // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetHostByName(PolyObject *threadId, PolyWord hName)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Look up a host name. */
    TempCString hostName(Poly_string_to_C_alloc(hName));
    struct hostent *host = gethostbyname(hostName);
    // If this fails the ML function returns NONE
    Handle result = host == NULL ? 0 : makeHostEntry(taskData, host);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkGetHostByAddr(PolyObject *threadId, PolyWord hostAddr)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();

    /* Look up entry by address. */
    unsigned long addr = htonl(get_C_unsigned(taskData, hostAddr));
    /* Look up a host name given an address. */
    struct hostent *host = gethostbyaddr((char*)&addr, sizeof(addr), AF_INET);
    Handle result = host == NULL ? 0 : makeHostEntry(taskData, host);

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

POLYUNSIGNED PolyNetworkCloseSocket(PolyObject *threadId, PolyWord strm)
{
    TaskData *taskData = TaskData::FindTaskForId(threadId);
    ASSERT(taskData != 0);
    taskData->PreRTSCall();
    Handle reset = taskData->saveVec.mark();
    Handle result = 0;
    Handle pushedStream = taskData->saveVec.push(strm);

    try {
        // This is defined to raise an exception if the socket has already been closed
#if (defined(_WIN32) && ! defined(__CYGWIN__))
        WinSocket *winskt = *(WinSocket**)(pushedStream->WordP());
        if (winskt != 0)
        {
            if (closesocket(winskt->getSocket()) != 0)
                raise_syscall(taskData, "Error during close", GETERROR);
        }
        else raise_syscall(taskData, "Socket is closed", WSAEBADF);
        *(WinSocket **)(pushedStream->WordP()) = 0; // Mark as closed
#else
        int descr = getStreamFileDescriptorWithoutCheck(pushedStream->Word());
        if (descr >= 0)
        {
            if (close(descr) != 0)
                raise_syscall(taskData, "Error during close", GETERROR);
        }
        else raise_syscall(taskData, "Socket is closed", EBADF);
        *(int*)(pushedStream->WordP()) = 0; // Mark as closed
#endif
        result = Make_fixed_precision(taskData, 0);
    }
    catch (...) {} // If an ML exception is raised

    taskData->saveVec.reset(reset);
    taskData->PostRTSCall();
    if (result == 0) return TAGGED(0).AsUnsigned();
    else return result->Word().AsUnsigned();
}

struct _entrypts networkingEPT[] =
{
    { "PolyNetworkGeneral",                     (polyRTSFunction)&PolyNetworkGeneral},
    { "PolyNetworkGetServByName",               (polyRTSFunction)&PolyNetworkGetServByName},
    { "PolyNetworkGetServByNameAndProtocol",    (polyRTSFunction)&PolyNetworkGetServByNameAndProtocol},
    { "PolyNetworkGetServByPort",               (polyRTSFunction)&PolyNetworkGetServByPort},
    { "PolyNetworkGetServByPortAndProtocol",    (polyRTSFunction)&PolyNetworkGetServByPortAndProtocol},
    { "PolyNetworkGetProtByName",               (polyRTSFunction)&PolyNetworkGetProtByName},
    { "PolyNetworkGetProtByNo",                 (polyRTSFunction)&PolyNetworkGetProtByNo},
    { "PolyNetworkGetHostName",                 (polyRTSFunction)&PolyNetworkGetHostName},
    { "PolyNetworkGetHostByName",               (polyRTSFunction)&PolyNetworkGetHostByName},
    { "PolyNetworkGetHostByAddr",               (polyRTSFunction)&PolyNetworkGetHostByAddr},
    { "PolyNetworkCloseSocket",                 (polyRTSFunction)&PolyNetworkCloseSocket },
    { "PolyNetworkSelect",                      (polyRTSFunction)&PolyNetworkSelect },
    { "PolyNetworkGetSocketError",              (polyRTSFunction)&PolyNetworkGetSocketError },
    { "PolyNetworkConnect",                     (polyRTSFunction)&PolyNetworkConnect },
    { "PolyNetworkAccept",                      (polyRTSFunction)&PolyNetworkAccept },
    { "PolyNetworkSend",                        (polyRTSFunction)&PolyNetworkSend },
    { "PolyNetworkSendTo",                      (polyRTSFunction)&PolyNetworkSendTo },

    { NULL, NULL} // End of list.
};

class Networking: public RtsModule
{
public:
    virtual void Init(void);
    virtual void Stop(void);
};

// Declare this.  It will be automatically added to the table.
static Networking networkingModule;

void Networking::Init(void)
{
#if (defined(_WIN32) && ! defined(__CYGWIN__))
#define WINSOCK_MAJOR_VERSION   2
#define WINSOCK_MINOR_VERSION   2
    WSADATA wsaData;
    WORD wVersion = MAKEWORD(WINSOCK_MINOR_VERSION, WINSOCK_MAJOR_VERSION);
    /* Initialise the system and check that the version it supplied
       is the one we requested. */
    if(WSAStartup(wVersion, &wsaData) == 0)
    {
        if (wsaData.wVersion == wVersion)
            winsock_init = 1;
        else WSACleanup();
    }
#endif
}

void Networking::Stop(void)
{
#if (defined(_WIN32) && ! defined(__CYGWIN__))
    if (winsock_init) WSACleanup();
    winsock_init = 0;
#endif
}
