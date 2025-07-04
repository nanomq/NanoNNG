// Copyright 2025 Jaylin <neverfail2012@hotmail.com>
// Copyright 2025 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef NNG_NNG_H
#define NNG_NNG_H

// NNG (nanomsg-next-gen) is an improved implementation of the SP protocols.
// The APIs have changed, and there is no attempt to provide API compatibility
// with legacy libnanomsg. This file defines the library consumer-facing
// Public API. Use of definitions or declarations not found in this header
// file is specifically unsupported and strongly discouraged.

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NNG_DECL is used on declarations to deal with scope.
// For building Windows DLLs, it should be the appropriate __declspec().
// For shared libraries with platforms that support hidden visibility,
// it should evaluate to __attribute__((visibility("default"))).
#ifndef NNG_DECL
#if defined(_WIN32) && !defined(NNG_STATIC_LIB)
#if defined(NNG_SHARED_LIB)
#define NNG_DECL __declspec(dllexport)
#else
#define NNG_DECL __declspec(dllimport)
#endif // NNG_SHARED_LIB
#else
#if defined(NNG_SHARED_LIB) && defined(NNG_HIDDEN_VISIBILITY)
#define NNG_DECL __attribute__((visibility("default")))
#else
#define NNG_DECL extern
#endif
#endif // _WIN32 && !NNG_STATIC_LIB
#endif // NNG_DECL

#ifndef NNG_DEPRECATED
#if defined(__GNUC__) || defined(__clang__)
#define NNG_DEPRECATED __attribute__((deprecated))
#else
#define NNG_DEPRECATED
#endif
#endif

// NNG Library & API version.
// We use SemVer, and these versions are about the API, and
// may not necessarily match the ABI versions.
#define NNG_MAJOR_VERSION 1
#define NNG_MINOR_VERSION 10
#define NNG_PATCH_VERSION 0
// if non-empty (i.e. "pre"), this is a pre-release
#define NNG_RELEASE_SUFFIX ""

// Maximum length of a socket address. This includes the terminating NUL.
// This limit is built into other implementations, so do not change it.
// Note that some transports are quite happy to let you use addresses
// in excess of this, but if you do you may not be able to communicate
// with other implementations.
#define NNG_MAXADDRLEN (128)

// NNG_PROTOCOL_NUMBER is used by protocol headers to calculate their
// protocol number from a major and minor number.  Applications should
// probably not need to use this.
#define NNG_PROTOCOL_NUMBER(maj, min) (((x) * 16) + (y))

// Types common to nng.

// Identifiers are wrapped in a structure to improve compiler validation
// of incorrect passing.  This gives us strong type checking.  Modern
// compilers compile passing these by value to identical code as passing
// the integer type (at least with optimization applied).  Please do not
// access the ID member directly.

typedef struct nng_ctx_s {
	uint32_t id;
} nng_ctx;

typedef struct nng_dialer_s {
	uint32_t id;
} nng_dialer;

typedef struct nng_listener_s {
	uint32_t id;
} nng_listener;

typedef struct nng_pipe_s {
	uint32_t id;
} nng_pipe;

typedef struct nng_socket_s {
	uint32_t id;
	void *   data;
} nng_socket;

typedef int32_t nng_duration; // in milliseconds

// nng_time represents an absolute time since some arbitrary point in the
// past, measured in milliseconds.  The values are always positive.
typedef uint64_t nng_time;

typedef struct nng_msg  nng_msg;
typedef struct nng_stat nng_stat;
typedef struct nng_aio  nng_aio;

// Initializers.
// clang-format off
#define NNG_PIPE_INITIALIZER { 0 }
#define NNG_SOCKET_INITIALIZER { 0 }
#define NNG_DIALER_INITIALIZER { 0 }
#define NNG_LISTENER_INITIALIZER { 0 }
#define NNG_CTX_INITIALIZER { 0 }
// clang-format on

// Some address details. This is in some ways like a traditional sockets
// sockaddr, but we have our own to cope with our unique families, etc.
// The details of this structure are directly exposed to applications.
// These structures can be obtained via property lookups, etc.
struct nng_sockaddr_inproc {
	uint16_t sa_family;
	char     sa_name[NNG_MAXADDRLEN];
};

struct nng_sockaddr_path {
	uint16_t sa_family;
	char     sa_path[NNG_MAXADDRLEN];
};

struct nng_sockaddr_in6 {
	uint16_t sa_family;
	uint16_t sa_port;
	uint8_t  sa_addr[16];
	uint32_t sa_scope;
};
struct nng_sockaddr_in {
	uint16_t sa_family;
	uint16_t sa_port;
	uint32_t sa_addr;
};

struct nng_sockaddr_zt {
	uint16_t sa_family;
	uint64_t sa_nwid;
	uint64_t sa_nodeid;
	uint32_t sa_port;
};

struct nng_sockaddr_abstract {
	uint16_t sa_family;
	uint16_t sa_len;       // will be 0 - 107 max.
	uint8_t  sa_name[107]; // 108 linux/windows, without leading NUL
};

// nng_sockaddr_storage is the size required to store any nng_sockaddr.
// This size must not change, and no individual nng_sockaddr type may grow
// larger than this without breaking binary compatibility.
struct nng_sockaddr_storage {
	uint16_t sa_family;
	uint64_t sa_pad[16];
};

typedef struct nng_sockaddr_inproc   nng_sockaddr_inproc;
typedef struct nng_sockaddr_path     nng_sockaddr_path;
typedef struct nng_sockaddr_path     nng_sockaddr_ipc;
typedef struct nng_sockaddr_in       nng_sockaddr_in;
typedef struct nng_sockaddr_in6      nng_sockaddr_in6;
typedef struct nng_sockaddr_zt       nng_sockaddr_zt;
typedef struct nng_sockaddr_abstract nng_sockaddr_abstract;
typedef struct nng_sockaddr_storage  nng_sockaddr_storage;

typedef union nng_sockaddr {
	uint16_t              s_family;
	nng_sockaddr_ipc      s_ipc;
	nng_sockaddr_inproc   s_inproc;
	nng_sockaddr_in6      s_in6;
	nng_sockaddr_in       s_in;
	nng_sockaddr_zt       s_zt;
	nng_sockaddr_abstract s_abstract;
	nng_sockaddr_storage  s_storage;
} nng_sockaddr;

enum nng_sockaddr_family {
	NNG_AF_UNSPEC   = 0,
	NNG_AF_INPROC   = 1,
	NNG_AF_IPC      = 2,
	NNG_AF_INET     = 3,
	NNG_AF_INET6    = 4,
	NNG_AF_ZT       = 5, // ZeroTier
	NNG_AF_ABSTRACT = 6
};

// Scatter/gather I/O.
typedef struct nng_iov {
	void  *iov_buf;
	size_t iov_len;
} nng_iov;

// Some definitions for durations used with timeouts.
#define NNG_DURATION_INFINITE (-1)
#define NNG_DURATION_DEFAULT (-2)
#define NNG_DURATION_ZERO (0)

// nng_fini is used to terminate the library, freeing certain global resources.
// This should only be called during atexit() or just before dlclose().
// THIS FUNCTION MUST NOT BE CALLED CONCURRENTLY WITH ANY OTHER FUNCTION
// IN THIS LIBRARY; IT IS NOT REENTRANT OR THREADSAFE.
//
// For most cases, this call is unnecessary, but it is provided to assist
// when debugging with memory checkers (e.g. valgrind).  Calling this
// function prevents global library resources from being reported incorrectly
// as memory leaks.  In those cases, we recommend doing this with atexit().
NNG_DECL void nng_fini(void);

// nng_close closes the socket, terminating all activity and
// closing any underlying connections and releasing any associated
// resources.
// We're not eliding this with NNG_ELIDE_DEPRECATED for now, because
// it would break far too many applications, as nng_socket_close is brand new.
NNG_DECL int nng_close(nng_socket);

// nng_socket_close is the *new* name for nng_close.  It should be used
// in new code, as nng_close will be removed in the next major release.
NNG_DECL int nng_socket_close(nng_socket);

// nng_socket_id returns the positive socket id for the socket, or -1
// if the socket is not valid.
NNG_DECL int nng_socket_id(nng_socket);

NNG_DECL int nng_socket_set(nng_socket, const char *, const void *, size_t);
NNG_DECL int nng_socket_set_bool(nng_socket, const char *, bool);
NNG_DECL int nng_socket_set_int(nng_socket, const char *, int);
NNG_DECL int nng_socket_set_size(nng_socket, const char *, size_t);
NNG_DECL int nng_socket_set_uint64(nng_socket, const char *, uint64_t);
NNG_DECL int nng_socket_set_string(nng_socket, const char *, const char *);
NNG_DECL int nng_socket_set_ptr(nng_socket, const char *, void *);
NNG_DECL int nng_socket_set_ms(nng_socket, const char *, nng_duration);
NNG_DECL int nng_socket_set_addr(
    nng_socket, const char *, const nng_sockaddr *);

NNG_DECL int nng_socket_get(nng_socket, const char *, void *, size_t *);
NNG_DECL int nng_socket_get_bool(nng_socket, const char *, bool *);
NNG_DECL int nng_socket_get_int(nng_socket, const char *, int *);
NNG_DECL int nng_socket_get_size(nng_socket, const char *, size_t *);
NNG_DECL int nng_socket_get_uint64(nng_socket, const char *, uint64_t *);
NNG_DECL int nng_socket_get_string(nng_socket, const char *, char **);
NNG_DECL int nng_socket_get_ptr(nng_socket, const char *, void **);
NNG_DECL int nng_socket_get_ms(nng_socket, const char *, nng_duration *);
NNG_DECL int nng_socket_get_addr(nng_socket, const char *, nng_sockaddr *);

// These functions are used on a socket to get information about it's
// identity, and the identity of the peer.  Few applications need these.
NNG_DECL int nng_socket_proto_id(nng_socket id, uint16_t *);
NNG_DECL int nng_socket_peer_id(nng_socket id, uint16_t *);
NNG_DECL int nng_socket_proto_name(nng_socket id, const char **);
NNG_DECL int nng_socket_peer_name(nng_socket id, const char **);
NNG_DECL int nng_socket_raw(nng_socket, bool *);

// Utility function for getting a printable form of the socket address
// for display in logs, etc.  It is not intended to be parsed, and the
// display format may change without notice.  Generally you should alow
// at least NNG_MAXADDRSTRLEN if you want to avoid typical truncations.
// It is still possible for very long IPC paths to be truncated, but that
// is an edge case and applications that pass such long paths should
// expect some truncation (but they may pass larger values).
#define NNG_MAXADDRSTRLEN (NNG_MAXADDRLEN + 16) // extra bytes for scheme
NNG_DECL const char *nng_str_sockaddr(
    const nng_sockaddr *sa, char *buf, size_t bufsz);

// Arguably the pipe callback functions could be handled as an option,
// but with the need to specify an argument, we find it best to unify
// this as a separate function to pass in the argument and the callback.
// Only one callback can be set on a given socket, and there is no way
// to retrieve the old value.
typedef enum {
	NNG_PIPE_EV_ADD_PRE,  // Called just before pipe added to socket
	NNG_PIPE_EV_ADD_POST, // Called just after pipe added to socket
	NNG_PIPE_EV_REM_POST, // Called just after pipe removed from socket
	NNG_PIPE_EV_NUM,      // Used internally, must be last.
} nng_pipe_ev;

typedef void (*nng_pipe_cb)(nng_pipe, nng_pipe_ev, void *);

// nng_pipe_notify registers a callback to be executed when the
// given event is triggered.  To watch for different events, register
// multiple times.  Each event can have at most one callback registered.
NNG_DECL int nng_pipe_notify(nng_socket, nng_pipe_ev, nng_pipe_cb, void *);

// nng_listen creates a listening endpoint with no special options,
// and starts it listening.  It is functionally equivalent to the legacy
// nn_bind(). The underlying endpoint is returned back to the caller in the
// endpoint pointer, if it is not NULL.  The flags are ignored at present.
NNG_DECL int nng_listen(nng_socket, const char *, nng_listener *, int);

// nng_dial creates a dialing endpoint, with no special options, and
// starts it dialing.  Dialers have at most one active connection at a time
// This is similar to the legacy nn_connect().  The underlying endpoint
// is returned back to the caller in the endpoint pointer, if it is not NULL.
// The flags may be NNG_FLAG_NONBLOCK to indicate that the first attempt to
// dial will be made in the background, returning control to the caller
// immediately.  In this case, if the connection fails, the function will
// keep retrying in the background.  (If the connection is dropped in either
// case, it will still be reconnected in the background -- only the initial
// connection attempt is normally synchronous.)
NNG_DECL int nng_dial(nng_socket, const char *, nng_dialer *, int);

// nng_dialer_create creates a new dialer, that is not yet started.
NNG_DECL int nng_dialer_create(nng_dialer *, nng_socket, const char *);

// nng_listener_create creates a new listener, that is not yet started.
NNG_DECL int nng_listener_create(nng_listener *, nng_socket, const char *);

// nng_dialer_start starts the endpoint dialing.  This is only possible if
// the dialer is not already dialing.
NNG_DECL int nng_dialer_start(nng_dialer, int);

// nng_listener_start starts the endpoint listening.  This is only possible if
// the listener is not already listening.
NNG_DECL int nng_listener_start(nng_listener, int);

// nng_dialer_close closes the dialer, shutting down all underlying
// connections and releasing all associated resources.
NNG_DECL int nng_dialer_close(nng_dialer);

// nng_listener_close closes the listener, shutting down all underlying
// connections and releasing all associated resources.
NNG_DECL int nng_listener_close(nng_listener);

// nng_dialer_id returns the positive dialer ID, or -1 if the dialer is
// invalid.
NNG_DECL int nng_dialer_id(nng_dialer);

// nng_listener_id returns the positive listener ID, or -1 if the listener is
// invalid.
NNG_DECL int nng_listener_id(nng_listener);

NNG_DECL int nng_dialer_set(nng_dialer, const char *, const void *, size_t);
NNG_DECL int nng_dialer_set_bool(nng_dialer, const char *, bool);
NNG_DECL int nng_dialer_set_int(nng_dialer, const char *, int);
NNG_DECL int nng_dialer_set_size(nng_dialer, const char *, size_t);
NNG_DECL int nng_dialer_set_uint64(nng_dialer, const char *, uint64_t);
NNG_DECL int nng_dialer_set_string(nng_dialer, const char *, const char *);
NNG_DECL int nng_dialer_set_ptr(nng_dialer, const char *, void *);
NNG_DECL int nng_dialer_set_ms(nng_dialer, const char *, nng_duration);
NNG_DECL int nng_dialer_set_addr(
    nng_dialer, const char *, const nng_sockaddr *);

NNG_DECL int nng_dialer_get(nng_dialer, const char *, void *, size_t *);
NNG_DECL int nng_dialer_get_bool(nng_dialer, const char *, bool *);
NNG_DECL int nng_dialer_get_int(nng_dialer, const char *, int *);
NNG_DECL int nng_dialer_get_size(nng_dialer, const char *, size_t *);
NNG_DECL int nng_dialer_get_uint64(nng_dialer, const char *, uint64_t *);
NNG_DECL int nng_dialer_get_string(nng_dialer, const char *, char **);
NNG_DECL int nng_dialer_get_ptr(nng_dialer, const char *, void **);
NNG_DECL int nng_dialer_get_ms(nng_dialer, const char *, nng_duration *);
NNG_DECL int nng_dialer_get_addr(nng_dialer, const char *, nng_sockaddr *);

NNG_DECL int nng_listener_set(
    nng_listener, const char *, const void *, size_t);
NNG_DECL int nng_listener_set_bool(nng_listener, const char *, bool);
NNG_DECL int nng_listener_set_int(nng_listener, const char *, int);
NNG_DECL int nng_listener_set_size(nng_listener, const char *, size_t);
NNG_DECL int nng_listener_set_uint64(nng_listener, const char *, uint64_t);
NNG_DECL int nng_listener_set_string(nng_listener, const char *, const char *);
NNG_DECL int nng_listener_set_ptr(nng_listener, const char *, void *);
NNG_DECL int nng_listener_set_ms(nng_listener, const char *, nng_duration);
NNG_DECL int nng_listener_set_addr(
    nng_listener, const char *, const nng_sockaddr *);

NNG_DECL int nng_listener_get(nng_listener, const char *, void *, size_t *);
NNG_DECL int nng_listener_get_bool(nng_listener, const char *, bool *);
NNG_DECL int nng_listener_get_int(nng_listener, const char *, int *);
NNG_DECL int nng_listener_get_size(nng_listener, const char *, size_t *);
NNG_DECL int nng_listener_get_uint64(nng_listener, const char *, uint64_t *);
NNG_DECL int nng_listener_get_string(nng_listener, const char *, char **);
NNG_DECL int nng_listener_get_ptr(nng_listener, const char *, void **);
NNG_DECL int nng_listener_get_ms(nng_listener, const char *, nng_duration *);
NNG_DECL int nng_listener_get_addr(nng_listener, const char *, nng_sockaddr *);

// nng_strerror returns a human-readable string associated with the error
// code supplied.
NNG_DECL const char *nng_strerror(int);

// nng_send sends (or arranges to send) the data on the socket.  Note that
// this function may (will!) return before any receiver has actually
// received the data.  The return value will be zero to indicate that the
// socket has accepted the entire data for send, or an errno to indicate
// failure.  The flags may include NNG_FLAG_NONBLOCK or NNG_FLAG_ALLOC.
// If the flag includes NNG_FLAG_ALLOC, then the function will call
// nng_free() on the supplied pointer & size on success. (If the call
// fails then the memory is not freed.)
NNG_DECL int nng_send(nng_socket, void *, size_t, int);

// nng_recv receives message data into the socket, up to the supplied size.
// The actual size of the message data will be written to the value pointed
// to by size.  The flags may include NNG_FLAG_NONBLOCK and NNG_FLAG_ALLOC.
// If NNG_FLAG_ALLOC is supplied then the library will allocate memory for
// the caller.  In that case the pointer to the allocated will be stored
// instead of the data itself.  The caller is responsible for freeing the
// associated memory with nng_free().
NNG_DECL int nng_recv(nng_socket, void *, size_t *, int);

// nng_sendmsg is like nng_send, but offers up a message structure, which
// gives the ability to provide more control over the message, including
// providing backtrace information.  It also can take a message that was
// obtained via nn_recvmsg, allowing for zero copy forwarding.
NNG_DECL int nng_sendmsg(nng_socket, nng_msg *, int);

// nng_recvmsg is like nng_recv, but is used to obtain a message structure
// as well as the data buffer.  This can be used to obtain more information
// about where the message came from, access raw headers, etc.  It also
// can be passed off directly to nng_sendmsg.
NNG_DECL int nng_recvmsg(nng_socket, nng_msg **, int);

// nng_sock_send sends data on the socket asynchronously.  As with nng_send,
// the completion may be executed before the data has actually been delivered,
// but only when it is accepted for delivery.  The supplied AIO must have
// been initialized, and have an associated message.  The message will be
// "owned" by the socket if the operation completes successfully.  Otherwise,
// the caller is responsible for freeing it.
NNG_DECL void nng_sock_send(nng_socket, nng_aio *);

// Compatible alias for nng_sock_send.
NNG_DECL void nng_send_aio(nng_socket, nng_aio *);

// nng_sock_recv receives data on the socket asynchronously.  On a successful
// result, the AIO will have an associated message, that can be obtained
// with nng_aio_get_msg().  The caller takes ownership of the message at
// this point.
NNG_DECL void nng_sock_recv(nng_socket, nng_aio *);

// Compatible alias for nng_sock_recv.
NNG_DECL void nng_recv_aio(nng_socket, nng_aio *);

// Context support.  User contexts are not supported by all protocols,
// but for those that do, they give a way to create multiple contexts
// on a single socket, each of which runs the protocol's state machinery
// independently, offering a way to achieve concurrent protocol support
// without resorting to raw mode sockets.  See the protocol specific
// documentation for further details.  (Note that at this time, only
// asynchronous send/recv are supported for contexts, but its easy enough
// to make synchronous versions with nng_aio_wait().)  Note that nng_close
// of the parent socket will *block* as long as any contexts are open.

// nng_ctx_open creates a context.  This returns NNG_ENOTSUP if the
// protocol implementation does not support separate contexts.
NNG_DECL int nng_ctx_open(nng_ctx *, nng_socket);

// move ctx from socket 1 to socket 2
// For dynamic bridging use only
NNG_DECL int nng_sock_replace(nng_socket, nng_socket);

// nng_ctx_close closes the context.
NNG_DECL int nng_ctx_close(nng_ctx);

// nng_ctx_id returns the numeric id for the context; this will be
// a positive value for a valid context, or < 0 for an invalid context.
// A valid context is not necessarily an *open* context.
NNG_DECL int nng_ctx_id(nng_ctx);

// nng_ctx_recv receives asynchronously.  It works like nng_sock_recv, but
// uses a local context instead of the socket global context.
NNG_DECL void nng_ctx_recv(nng_ctx, nng_aio *);

// nng_ctx_recvmsg allows for receiving a message synchronously using
// a context.  It has the same semantics as nng_recvmsg, but operates
// on a context instead of a socket.
NNG_DECL int nng_ctx_recvmsg(nng_ctx, nng_msg **, int);

// nng_ctx_send sends asynchronously. It works like nng_sock_send, but
// uses a local context instead of the socket global context.
NNG_DECL void nng_ctx_send(nng_ctx, nng_aio *);

// nng_ctx_sendmsg is allows for sending a message synchronously using
// a context.  It has the same semantics as nng_sendmsg, but operates
// on a context instead of a socket.
NNG_DECL int nng_ctx_sendmsg(nng_ctx, nng_msg *, int);

NNG_DECL int nng_ctx_get(nng_ctx, const char *, void *, size_t *);
NNG_DECL int nng_ctx_get_bool(nng_ctx, const char *, bool *);
NNG_DECL int nng_ctx_get_int(nng_ctx, const char *, int *);
NNG_DECL int nng_ctx_get_size(nng_ctx, const char *, size_t *);
NNG_DECL int nng_ctx_get_uint64(nng_ctx, const char *, uint64_t *);
NNG_DECL int nng_ctx_get_string(nng_ctx, const char *, char **);
NNG_DECL int nng_ctx_get_ptr(nng_ctx, const char *, void **);
NNG_DECL int nng_ctx_get_ms(nng_ctx, const char *, nng_duration *);

NNG_DECL int nng_ctx_set(nng_ctx, const char *, const void *, size_t);
NNG_DECL int nng_ctx_set_bool(nng_ctx, const char *, bool);
NNG_DECL int nng_ctx_set_int(nng_ctx, const char *, int);
NNG_DECL int nng_ctx_set_size(nng_ctx, const char *, size_t);
NNG_DECL int nng_ctx_set_uint64(nng_ctx, const char *, uint64_t);
NNG_DECL int nng_ctx_set_string(nng_ctx, const char *, const char *);
NNG_DECL int nng_ctx_set_ptr(nng_ctx, const char *, void *);
NNG_DECL int nng_ctx_set_ms(nng_ctx, const char *, nng_duration);

// nng_alloc is used to allocate memory.  It's intended purpose is for
// allocating memory suitable for message buffers with nng_send().
// Applications that need memory for other purposes should use their platform
// specific API.
NNG_DECL void *nng_alloc(size_t);

// nng_free is used to free memory allocated with nng_alloc, which includes
// memory allocated by nng_recv() when the NNG_FLAG_ALLOC message is supplied.
// As the application is required to keep track of the size of memory, this
// is probably less convenient for general uses than the C library malloc and
// calloc.
NNG_DECL void *nng_zalloc(size_t sz);
NNG_DECL void  nng_zfree(void *ptr);
NNG_DECL void  nng_free(void *, size_t);

// nng_strdup duplicates the source string, using nng_alloc. The result
// should be freed with nng_strfree (or nng_free(strlen(s)+1)).
NNG_DECL char *nng_strdup(const char *);
NNG_DECL char *nng_strndup(const char *, size_t);
NNG_DECL char *nng_strnins(char *, const char *, size_t, size_t);
NNG_DECL char *nng_strncat(char *, const char *, size_t, size_t);

// nng_strfree is equivalent to nng_free(strlen(s)+1).
NNG_DECL void nng_strfree(char *);

NNG_DECL char *nng_strcasestr(const char *, const char *);
NNG_DECL int   nng_strcasecmp(const char *, const char *);
NNG_DECL int   nng_strncasecmp(const char *, const char *, size_t);

// Async IO API.  AIO structures can be thought of as "handles" to
// support asynchronous operations.  They contain the completion callback, and
// a pointer to consumer data.  This is similar to how overlapped I/O
// works in Windows, when used with a completion callback.
//
// AIO structures can carry up to 4 distinct input values, and up to
// 4 distinct output values, and up to 4 distinct "private state" values.
// The meaning of the inputs and the outputs are determined by the
// I/O functions being called.

// nng_aio_alloc allocates a new AIO, and associated the completion
// callback and its opaque argument.  If NULL is supplied for the
// callback, then the caller must use nng_aio_wait() to wait for the
// operation to complete.  If the completion callback is not NULL, then
// when a submitted operation completes (or is canceled or fails) the
// callback will be executed, generally in a different thread, with no
// locks held.
NNG_DECL int nng_aio_alloc(nng_aio **, void (*)(void *), void *);

// nng_aio_free frees the AIO and any associated resources.
// It *must not* be in use at the time it is freed.
NNG_DECL void nng_aio_free(nng_aio *);

// nng_aio_reap is like nng_aio_free, but calls it from a background
// reaper thread.  This can be useful to free aio objects from aio
// callbacks (e.g. when the result of the callback is to discard
// the object in question.)  The aio object must be in further use
// when this is called.
NNG_DECL void nng_aio_reap(nng_aio *);

// nng_aio_stop stops any outstanding operation, and waits for the
// AIO to be free, including for the callback to have completed
// execution.  Therefore, the caller must NOT hold any locks that
// are acquired in the callback, or deadlock will occur.
NNG_DECL void nng_aio_stop(nng_aio *);

// nng_aio_result returns the status/result of the operation. This
// will be zero on successful completion, or an nng error code on
// failure.
NNG_DECL int nng_aio_result(nng_aio *);

// nng_aio_count returns the number of bytes transferred for certain
// I/O operations.  This is meaningless for other operations (e.g.
// DNS lookups or TCP connection setup).
NNG_DECL size_t nng_aio_count(nng_aio *);

// nng_aio_cancel attempts to cancel any in-progress I/O operation.
// The AIO callback will still be executed, but if the cancellation is
// successful then the status will be NNG_ECANCELED.
NNG_DECL void nng_aio_cancel(nng_aio *);

// nng_aio_abort is like nng_aio_cancel, but allows for a different
// error result to be returned.
NNG_DECL void nng_aio_abort(nng_aio *, int);

// nng_aio_wait waits synchronously for any pending operation to complete.
// It also waits for the callback to have completed execution.  Therefore,
// the caller of this function must not hold any locks acquired by the
// callback or deadlock may occur.
NNG_DECL void nng_aio_wait(nng_aio *);

// nng_aio_busy returns true if the aio is still busy processing the
// operation, or executing associated completion functions.  Note that
// if the completion function schedules a new operation using the aio,
// then this function will continue to return true.
NNG_DECL bool nng_aio_busy(nng_aio *);

// nng_aio_set_msg sets the message structure to use for asynchronous
// message send operations.
NNG_DECL void nng_aio_set_msg(nng_aio *, nng_msg *);

// nng_aio_get_msg returns the message structure associated with a completed
// receive operation.
NNG_DECL nng_msg *nng_aio_get_msg(nng_aio *);

// nng_aio_set_input sets an input parameter at the given index.
NNG_DECL int nng_aio_set_input(nng_aio *, unsigned, void *);

// nng_aio_get_input retrieves the input parameter at the given index.
NNG_DECL void *nng_aio_get_input(nng_aio *, unsigned);

// nng_aio_set_output sets an output result at the given index.
NNG_DECL int nng_aio_set_output(nng_aio *, unsigned, void *);

// nng_aio_get_output retrieves the output result at the given index.
NNG_DECL void *nng_aio_get_output(nng_aio *, unsigned);

NNG_DECL void  nng_aio_set_prov_data(nng_aio *, void *);
NNG_DECL void *nng_aio_get_prov_data(nng_aio *);

// nng_aio_set_timeout sets a timeout on the AIO.  This should be called for
// operations that should time out after a period.  The timeout should be
// either a positive number of milliseconds, or NNG_DURATION_INFINITE to
// indicate that the operation has no timeout.  A poll may be done by
// specifying NNG_DURATION_ZERO.  The value NNG_DURATION_DEFAULT indicates
// that any socket specific timeout should be used.
NNG_DECL void nng_aio_set_timeout(nng_aio *, nng_duration);

// nng_aio_set_expire is like nng_aio_set_timeout, except it sets an absolute
// expiration time.  This is useful when chaining actions on a single aio
// as part of a state machine.
NNG_DECL void nng_aio_set_expire(nng_aio *, nng_time);

// nng_aio_set_iov sets a scatter/gather vector on the aio.  The iov array
// itself is copied. Data members (the memory regions referenced) *may* be
// copied as well, depending on the operation.  This operation is guaranteed
// to succeed if n <= 4, otherwise it may fail due to NNG_ENOMEM.
NNG_DECL int nng_aio_set_iov(nng_aio *, unsigned, const nng_iov *);

// nng_aio_begin is called by the provider to mark the operation as
// beginning.  If it returns false, then the provider must take no
// further action on the aio.
NNG_DECL bool nng_aio_begin(nng_aio *);

// nng_aio_finish is used to "finish" an asynchronous operation.
// It should only be called by "providers" (such as HTTP server API users).
// The argument is the value that nng_aio_result() should return.
// IMPORTANT: Callers must ensure that this is called EXACTLY ONCE on any
// given aio.
NNG_DECL void nng_aio_finish(nng_aio *, int);

// nng_aio_defer is used to register a cancellation routine, and indicate
// that the operation will be completed asynchronously.  It must only be
// called once per operation on an aio, and must only be called by providers.
// If the operation is canceled by the consumer, the cancellation callback
// will be called.  The provider *must* still ensure that the nng_aio_finish()
// function is called EXACTLY ONCE.  If the operation cannot be canceled
// for any reason, the cancellation callback should do nothing.  The
// final argument is passed to the cancelfn.  The final argument of the
// cancellation function is the error number (will not be zero) corresponding
// to the reason for cancellation, e.g. NNG_ETIMEDOUT or NNG_ECANCELED.
typedef void  (*nng_aio_cancelfn)(nng_aio *, void *, int);
NNG_DECL void nng_aio_defer(nng_aio *, nng_aio_cancelfn, void *);

// nng_aio_sleep does a "sleeping" operation, basically does nothing
// but wait for the specified number of milliseconds to expire, then
// calls the callback.  This returns 0, rather than NNG_ETIMEDOUT.
NNG_DECL void nng_sleep_aio(nng_duration, nng_aio *);

// Message API.
NNG_DECL int      nng_msg_alloc(nng_msg **, size_t);
NNG_DECL void     nng_msg_free(nng_msg *);
NNG_DECL int      nng_msg_realloc(nng_msg *, size_t);
NNG_DECL int      nng_msg_reserve(nng_msg *, size_t);
NNG_DECL size_t   nng_msg_capacity(nng_msg *);
NNG_DECL void    *nng_msg_header(nng_msg *);
NNG_DECL size_t   nng_msg_header_len(const nng_msg *);
NNG_DECL void    *nng_msg_body(nng_msg *);
NNG_DECL size_t   nng_msg_len(const nng_msg *);
NNG_DECL int      nng_msg_append(nng_msg *, const void *, size_t);
NNG_DECL int      nng_msg_insert(nng_msg *, const void *, size_t);
NNG_DECL int      nng_msg_trim(nng_msg *, size_t);
NNG_DECL int      nng_msg_chop(nng_msg *, size_t);
NNG_DECL int      nng_msg_header_append(nng_msg *, const void *, size_t);
NNG_DECL int      nng_msg_header_insert(nng_msg *, const void *, size_t);
NNG_DECL int      nng_msg_header_trim(nng_msg *, size_t);
NNG_DECL int      nng_msg_header_chop(nng_msg *, size_t);
NNG_DECL int      nng_msg_header_append_u16(nng_msg *, uint16_t);
NNG_DECL int      nng_msg_header_append_u32(nng_msg *, uint32_t);
NNG_DECL int      nng_msg_header_append_u64(nng_msg *, uint64_t);
NNG_DECL int      nng_msg_header_insert_u16(nng_msg *, uint16_t);
NNG_DECL int      nng_msg_header_insert_u32(nng_msg *, uint32_t);
NNG_DECL int      nng_msg_header_insert_u64(nng_msg *, uint64_t);
NNG_DECL int      nng_msg_header_chop_u16(nng_msg *, uint16_t *);
NNG_DECL int      nng_msg_header_chop_u32(nng_msg *, uint32_t *);
NNG_DECL int      nng_msg_header_chop_u64(nng_msg *, uint64_t *);
NNG_DECL int      nng_msg_header_trim_u16(nng_msg *, uint16_t *);
NNG_DECL int      nng_msg_header_trim_u32(nng_msg *, uint32_t *);
NNG_DECL int      nng_msg_header_trim_u64(nng_msg *, uint64_t *);
NNG_DECL int      nng_msg_append_u16(nng_msg *, uint16_t);
NNG_DECL int      nng_msg_append_u32(nng_msg *, uint32_t);
NNG_DECL int      nng_msg_append_u64(nng_msg *, uint64_t);
NNG_DECL int      nng_msg_insert_u16(nng_msg *, uint16_t);
NNG_DECL int      nng_msg_insert_u32(nng_msg *, uint32_t);
NNG_DECL int      nng_msg_insert_u64(nng_msg *, uint64_t);
NNG_DECL int      nng_msg_chop_u16(nng_msg *, uint16_t *);
NNG_DECL int      nng_msg_chop_u32(nng_msg *, uint32_t *);
NNG_DECL int      nng_msg_chop_u64(nng_msg *, uint64_t *);
NNG_DECL int      nng_msg_trim_u16(nng_msg *, uint16_t *);
NNG_DECL int      nng_msg_trim_u32(nng_msg *, uint32_t *);
NNG_DECL int      nng_msg_trim_u64(nng_msg *, uint64_t *);
NNG_DECL int      nng_msg_dup(nng_msg **, const nng_msg *);
NNG_DECL void     nng_msg_clear(nng_msg *);
NNG_DECL void     nng_msg_header_clear(nng_msg *);
NNG_DECL void     nng_msg_set_pipe(nng_msg *, nng_pipe);
NNG_DECL nng_pipe nng_msg_get_pipe(const nng_msg *);

// Pipe API. Generally pipes are only "observable" to applications, but
// we do permit an application to close a pipe. This can be useful, for
// example during a connection notification, to disconnect a pipe that
// is associated with an invalid or untrusted remote peer.
NNG_DECL int nng_pipe_get(nng_pipe, const char *, void *, size_t *);
NNG_DECL int nng_pipe_get_bool(nng_pipe, const char *, bool *);
NNG_DECL int nng_pipe_get_int(nng_pipe, const char *, int *);
NNG_DECL int nng_pipe_get_ms(nng_pipe, const char *, nng_duration *);
NNG_DECL int nng_pipe_get_size(nng_pipe, const char *, size_t *);
NNG_DECL int nng_pipe_get_uint64(nng_pipe, const char *, uint64_t *);
NNG_DECL int nng_pipe_get_string(nng_pipe, const char *, char **);
NNG_DECL int nng_pipe_get_ptr(nng_pipe, const char *, void **);
NNG_DECL int nng_pipe_get_addr(nng_pipe, const char *, nng_sockaddr *);

NNG_DECL int          nng_pipe_close(nng_pipe);
NNG_DECL int          nng_pipe_id(nng_pipe);
NNG_DECL nng_socket   nng_pipe_socket(nng_pipe);
NNG_DECL nng_dialer   nng_pipe_dialer(nng_pipe);
NNG_DECL nng_listener nng_pipe_listener(nng_pipe);

// Flags.
#define NNG_FLAG_ALLOC 1u    // Recv to allocate receive buffer
#define NNG_FLAG_NONBLOCK 2u // Non-blocking operations

// Options.
#define NNG_OPT_SOCKNAME "socket-name"
#define NNG_OPT_RAW "raw"
#define NNG_OPT_PROTO "protocol"
#define NNG_OPT_PROTONAME "protocol-name"
#define NNG_OPT_PEER "peer"
#define NNG_OPT_PEERNAME "peer-name"
#define NNG_OPT_RECVBUF "recv-buffer"
#define NNG_OPT_SENDBUF "send-buffer"
#define NNG_OPT_RECVFD "recv-fd"
#define NNG_OPT_SENDFD "send-fd"
#define NNG_OPT_RECVTIMEO "recv-timeout"
#define NNG_OPT_SENDTIMEO "send-timeout"
#define NNG_OPT_LOCADDR "local-address"
#define NNG_OPT_REMADDR "remote-address"
#define NNG_OPT_URL "url"
#define NNG_OPT_BRIDGE_SET_EP_CLOSED "ep-closed-switch"
#define NNG_OPT_MAXTTL "ttl-max"
#define NNG_OPT_RECVMAXSZ "recv-size-max"
#define NNG_OPT_RECONNMINT "reconnect-time-min"
#define NNG_OPT_RECONNMAXT "reconnect-time-max"

// NNG-MQTT
#define NNG_OPT_MQTT_CONNMSG "mqtt-connect-msg"
#define NNG_OPT_MQTT_BRIDGE_CONF "mqtt-bridge-config"

// NNG-QUIC
#define NNG_OPT_QUIC_ENABLE_0RTT "quic-0rtt"
#define NNG_OPT_QUIC_ENABLE_MULTISTREAM "quic-multistream"
#define NNG_OPT_QUIC_IDLE_TIMEOUT "quic-idle-timeout"
#define NNG_OPT_QUIC_KEEPALIVE "quic-keepalive"
#define NNG_OPT_QUIC_CONNECT_TIMEOUT "quic-connect-timeout"
#define NNG_OPT_QUIC_DISCONNECT_TIMEOUT "quic-disconnect-timeout"
#define NNG_OPT_QUIC_SEND_IDLE_TIMEOUT "quic-send-idle-timeout"
#define NNG_OPT_QUIC_INITIAL_RTT_MS "quic-initial-ms"
#define NNG_OPT_QUIC_MAX_ACK_DELAY_MS "quic-max-ack-delay-ms"
#define NNG_OPT_MQTT_QUIC_PRIORITY "quic-mqtt-stream-priority"
#define NNG_OPT_QUIC_PRIORITY "quic-stream-priority"

#define NNG_OPT_QUIC_CONGESTION_CTL_CUBIC "quic-congestion-cubic"

#define NNG_OPT_QUIC_TLS_CACERT_PATH "quic-tls-cacert"
#define NNG_OPT_QUIC_TLS_KEY_PATH "quic-tls-key"
#define NNG_OPT_QUIC_TLS_KEY_PASSWORD "quic-tls-pwd"
#define NNG_OPT_QUIC_TLS_VERIFY_PEER "quic-tls-verify"
#define NNG_OPT_QUIC_TLS_CA_PATH "quic-tls-ca"

// TLS options are only used when the underlying transport supports TLS.

// NNG_OPT_TLS_CONFIG is a pointer to a nng_tls_config object.  Generally
// this can be used with endpoints, although once an endpoint is started, or
// once a configuration is used, the value becomes read-only. Note that
// when configuring the object, a hold is placed on the TLS configuration,
// using a reference count.  When retrieving the object, no such hold is
// placed, and so the caller must take care not to use the associated object
// after the endpoint it is associated with is closed.
#define NNG_OPT_TLS_CONFIG "tls-config"

// NNG_OPT_TLS_AUTH_MODE is a write-only integer (int) option that specifies
// whether peer authentication is needed.  The option can take one of the
// values of NNG_TLS_AUTH_MODE_NONE, NNG_TLS_AUTH_MODE_OPTIONAL, or
// NNG_TLS_AUTH_MODE_REQUIRED.  The default is typically NNG_TLS_AUTH_MODE_NONE
// for listeners, and NNG_TLS_AUTH_MODE_REQUIRED for dialers. If set to
// REQUIRED, then connections will be rejected if the peer cannot be verified.
// If set to OPTIONAL, then a verification step takes place, but the connection
// is still permitted.  (The result can be checked with NNG_OPT_TLS_VERIFIED).
#define NNG_OPT_TLS_AUTH_MODE "tls-authmode"

// NNG_OPT_TLS_CERT_KEY_FILE names a single file that contains a certificate
// and key identifying the endpoint.  This is a write-only value.  This can be
// set multiple times for different keys/certs corresponding to
// different algorithms on listeners, whereas dialers only support one.  The
// file must contain both cert and key as PEM blocks, and the key must
// not be encrypted.  (If more flexibility is needed, use the TLS configuration
// directly, via NNG_OPT_TLS_CONFIG.)
#define NNG_OPT_TLS_CERT_KEY_FILE "tls-cert-key-file"

// NNG_OPT_TLS_CA_FILE names a single file that contains certificate(s) for a
// CA, and optionally CRLs, which are used to validate the peer's certificate.
// This is a write-only value, but multiple CAs can be loaded by setting this
// multiple times.
#define NNG_OPT_TLS_CA_FILE "tls-ca-file"

// NNG_OPT_TLS_SERVER_NAME is a write-only string that can typically be
// set on dialers to check the CN of the server for a match.  This
// can also affect SNI (server name indication).  It usually has no effect
// on listeners.
#define NNG_OPT_TLS_SERVER_NAME "tls-server-name"

// NNG_OPT_TLS_VERIFIED returns a boolean indicating whether the peer has
// been verified (true) or not (false). Typically, this is read-only, and
// only available for pipes. This option may return incorrect results if
// peer authentication is disabled with `NNG_TLS_AUTH_MODE_NONE`.
#define NNG_OPT_TLS_VERIFIED "tls-verified"

// NNG_OPT_TLS_PEER_CN returns the string with the common name
// of the peer certificate. Typically, this is read-only and
// only available for pipes. This option may return incorrect results if
// peer authentication is disabled with `NNG_TLS_AUTH_MODE_NONE`.
#define NNG_OPT_TLS_PEER_CN "tls-peer-cn"

// NNG_OPT_TLS_PEER_ALT_NAMES returns string list with the
// subject alternative names of the peer certificate. Typically this is
// read-only and only available for pipes. This option may return
// incorrect results if peer authentication is disabled with
// `NNG_TLS_AUTH_MODE_NONE`.
#define NNG_OPT_TLS_PEER_ALT_NAMES "tls-peer-alt-names"

// TCP options.  These may be supported on various transports that use
// TCP underneath such as TLS, or not.

// TCP nodelay disables the use of Nagle, so that messages are sent
// as soon as data is available. This tends to reduce latency, but
// can come at the cost of extra messages being sent, and may have
// a detrimental effect on performance. For most uses, we recommend
// enabling this. (Disable it if you are on a very slow network.)
// This is a boolean.
#define NNG_OPT_TCP_NODELAY "tcp-nodelay"

// TCP keepalive causes the underlying transport to send keep-alive
// messages, and keep the session active. Keepalives are zero length
// messages with the ACK flag turned on. If we don't get an ACK back,
// then we know the other side is gone. This is useful for detecting
// dead peers, and is also used to prevent disconnections caused by
// middle boxes thinking the session has gone idle (e.g. keeping NAT
// state current). This is a boolean.
#define NNG_OPT_TCP_KEEPALIVE "tcp-keepalive"

// TODO: more notes
#define NNG_OPT_TCP_QUICKACK "tcp-quickack"

#define NNG_OPT_TCP_KEEPIDLE "tcp-keepidle"

#define NNG_OPT_TCP_KEEPINTVL "tcp-keepintvl"

#define NNG_OPT_TCP_KEEPCNT "tcp-keepcnt"

#define NNG_OPT_TCP_SENDTIMEO "tcp-sendtimeo"

#define NNG_OPT_TCP_RECVTIMEO "tcp-recvtimeo"

#define NNG_OPT_TCP_BINDTODEVICE "tcp-bindtodevice"

// Local TCP port number.  This is used on a listener, and is intended
// to be used after starting the listener in combination with a wildcard
// (0) local port.  This determines the actual ephemeral port that was
// selected and bound.  The value is provided as an int, but only the
// low order 16 bits will be set.  This is provided in native byte order,
// which makes it more convenient than using the NNG_OPT_LOCADDR option.
#define NNG_OPT_TCP_BOUND_PORT "tcp-bound-port"

// IPC options.  These will largely vary depending on the platform,
// as POSIX systems have very different options than Windows.

// Security Descriptor.  This option may only be set on listeners
// on the Windows platform, where the object is a pointer to a
// a Windows SECURITY_DESCRIPTOR.
#define NNG_OPT_IPC_SECURITY_DESCRIPTOR "ipc:security-descriptor"

// Permissions bits.  This option is only valid for listeners on
// POSIX platforms and others that honor UNIX style permission bits.
// Note that some platforms may not honor the permissions here, although
// at least Linux and macOS seem to do so.  Check before you rely on
// this for security.
#define NNG_OPT_IPC_PERMISSIONS "ipc:permissions"

// IPC peer options may also be used in some cases with other socket types.

// Peer UID.  This is only available on POSIX style systems.
#define NNG_OPT_PEER_UID "ipc:peer-uid"
#define NNG_OPT_IPC_PEER_UID NNG_OPT_PEER_UID

// Peer GID (primary group).  This is only available on POSIX style systems.
#define NNG_OPT_PEER_GID "ipc:peer-gid"
#define NNG_OPT_IPC_PEER_GID NNG_OPT_PEER_GID

// Peer process ID.  Available on Windows, Linux, and SunOS.
// In theory, we could obtain this with the first message sent,
// but we have elected not to do this for now. (Nice RFE for a FreeBSD
// guru though.)
#define NNG_OPT_PEER_PID "ipc:peer-pid"
#define NNG_OPT_IPC_PEER_PID NNG_OPT_PEER_PID

// Peer Zone ID.  Only on SunOS systems.  (Linux containers have no
// definable kernel identity; they are a user-land fabrication made up
// from various pieces of different namespaces. FreeBSD does have
// something called JailIDs, but it isn't obvious how to determine this,
// or even if processes can use IPC across jail boundaries.)
#define NNG_OPT_PEER_ZONEID "ipc:peer-zoneid"
#define NNG_OPT_IPC_PEER_ZONEID NNG_OPT_PEER_ZONEID

// WebSocket Options.

// NNG_OPT_WS_REQUEST_HEADERS is a string containing the
// request headers, formatted as CRLF terminated lines.
#define NNG_OPT_WS_REQUEST_HEADERS "ws:request-headers"

// NNG_OPT_WS_RESPONSE_HEADERS is a string containing the
// response headers, formatted as CRLF terminated lines.
#define NNG_OPT_WS_RESPONSE_HEADERS "ws:response-headers"

// NNG_OPT_WS_REQUEST_HEADER is a prefix, for a dynamic
// property name.  This allows direct access to any named header.
// Concatenate this with the name of the property (case is not sensitive).
// Only the first such header is returned.
#define NNG_OPT_WS_RESPONSE_HEADER "ws:response-header:"

// NNG_OPT_WS_RESPONSE_HEADER is like NNG_OPT_REQUEST_HEADER, but used for
// accessing the request headers.
#define NNG_OPT_WS_REQUEST_HEADER "ws:request-header:"

// NNG_OPT_WS_REQUEST_URI is used to obtain the URI sent by the client.
// This can be useful when a handler supports an entire directory tree.
#define NNG_OPT_WS_REQUEST_URI "ws:request-uri"

// NNG_OPT_WS_SENDMAXFRAME is used to configure the fragmentation size
// used for frames.  This has a default value of 64k.  Large values
// are good for throughput, but penalize latency.  They also require
// additional buffering on the peer.  This value must not be larger
// than what the peer will accept, and unfortunately there is no way
// to negotiate this.
#define NNG_OPT_WS_SENDMAXFRAME "ws:txframe-max"

// NNG_OPT_WS_RECVMAXFRAME is the largest frame we will accept.  This should
// probably not be larger than NNG_OPT_RECVMAXSZ. If the sender attempts
// to send more data than this in a single message, it will be dropped.
#define NNG_OPT_WS_RECVMAXFRAME "ws:rxframe-max"

// NNG_OPT_WS_PROTOCOL is the "websocket sub-protocol" -- it's a string.
// This is also known as the Sec-WebSocket-Protocol header. It is treated
// specially.  This is part of the websocket handshake.
#define NNG_OPT_WS_PROTOCOL "ws:protocol"

// NNG_OPT_WS_SEND_TEXT is a boolean used to tell the WS stream
// transport to send text messages.  This is not supported for the
// core WebSocket transport, but when using streams it might be useful
// to speak with 3rd party WebSocket applications.  This mode should
// not be used unless absolutely required. No validation of the message
// contents is performed by NNG; applications are expected to honor
// the requirement to send only valid UTF-8.  (Compliant applications
// will close the socket if they see this message type with invalid UTF-8.)
#define NNG_OPT_WS_SEND_TEXT "ws:send-text"

// NNG_OPT_WS_RECV_TEXT is a boolean that enables NNG to receive
// TEXT frames.  This is only useful for stream mode applications --
// SP protocol requires the use of binary frames.  Note also that
// NNG does not validate the message contents for valid UTF-8; this
// means it will not be conformant with RFC-6455 on it's own. Applications
// that need this should check the message contents themselves, and
// close the connection if invalid UTF-8 is received.  This option
// should not be used unless required to communication with 3rd party
// peers that cannot be coerced into sending binary frames.
#define NNG_OPT_WS_RECV_TEXT "ws:recv-text"

// NNG_OPT_SOCKET_FD is a write-only integer property that is used to
// file descriptors (or FILE HANDLE objects on Windows) to a
// socket:// based listener.  This file descriptor will be taken
// over and used as a stream connection.  The protocol is compatible
// with SP over TCP.  This facility is experimental, and intended to
// allow use with descriptors created via socketpair() or similar.
// Note that unidirectional pipes (such as those from pipe(2) or mkfifo)
// are not supported.
#define NNG_OPT_SOCKET_FD "socket:fd"

// XXX: TBD: priorities, ipv4only

// Statistics. These are for informational purposes only, and subject
// to change without notice. The API for accessing these is stable,
// but the individual statistic names, values, and meanings are all
// subject to change.

// nng_stats_get takes a snapshot of the entire set of statistics.
// While the operation can be somewhat expensive (allocations), it
// is done in a way that minimizes impact to running operations.
// Note that the statistics are provided as a tree, with parents
// used for grouping, and with child statistics underneath.  The
// top stat returned will be of type NNG_STAT_SCOPE with name "".
// Applications may choose to consider this root scope as "root", if
// the empty string is not suitable.
NNG_DECL int nng_stats_get(nng_stat **);

// nng_stats_free frees a previous list of snapshots.  This should only
// be called on the parent statistic that obtained via nng_stats_get.
NNG_DECL void nng_stats_free(nng_stat *);

// nng_stats_dump is a debugging function that dumps the entire set of
// statistics to stdout.
NNG_DECL void nng_stats_dump(nng_stat *);

// nng_stat_next finds the next sibling for the current stat.  If there
// are no more siblings, it returns NULL.
NNG_DECL nng_stat *nng_stat_next(nng_stat *);

// nng_stat_child finds the first child of the current stat.  If no children
// exist, then NULL is returned.
NNG_DECL nng_stat *nng_stat_child(nng_stat *);

// nng_stat_name is used to determine the name of the statistic.
// This is a human-readable name.  Statistic names, as well as the presence
// or absence or semantic of any particular statistic are not part of any
// stable API, and may be changed without notice in future updates.
NNG_DECL const char *nng_stat_name(nng_stat *);

// nng_stat_type is used to determine the type of the statistic.
// Counters generally increment, and therefore changes in the value over
// time are likely more interesting than the actual level.  Level
// values reflect some absolute state however, and should be presented to the
// user as is.
NNG_DECL int nng_stat_type(nng_stat *);

// nng_stat_find is used to find a specific named statistic within
// a statistic tree.  NULL is returned if no such statistic exists.
NNG_DECL nng_stat *nng_stat_find(nng_stat *, const char *);

// nng_stat_find_socket is used to find the stats for the given socket.
NNG_DECL nng_stat *nng_stat_find_socket(nng_stat *, nng_socket);

// nng_stat_find_dialer is used to find the stats for the given dialer.
NNG_DECL nng_stat *nng_stat_find_dialer(nng_stat *, nng_dialer);

// nng_stat_find_dialer is used to find the stats for the given dialer.
NNG_DECL nng_stat *nng_stat_find_pipe(nng_stat *, uint64_t pipeid);

// nng_stat_find_listener is used to find the stats for the given listener.
NNG_DECL nng_stat *nng_stat_find_listener(nng_stat *, nng_listener);

enum nng_stat_type_enum {
	NNG_STAT_SCOPE   = 0, // Stat is for scoping, and carries no value
	NNG_STAT_LEVEL   = 1, // Numeric "absolute" value, diffs meaningless
	NNG_STAT_COUNTER = 2, // Incrementing value (diffs are meaningful)
	NNG_STAT_STRING  = 3, // Value is a string
	NNG_STAT_BOOLEAN = 4, // Value is a boolean
	NNG_STAT_ID      = 5, // Value is a numeric ID
};

// nng_stat_unit provides information about the unit for the statistic,
// such as NNG_UNIT_BYTES or NNG_UNIT_BYTES.  If no specific unit is
// applicable, such as a relative priority, then NN_UNIT_NONE is returned.
NNG_DECL int nng_stat_unit(nng_stat *);

enum nng_unit_enum {
	NNG_UNIT_NONE     = 0, // No special units
	NNG_UNIT_BYTES    = 1, // Bytes, e.g. bytes sent, etc.
	NNG_UNIT_MESSAGES = 2, // Messages, one per message
	NNG_UNIT_MILLIS   = 3, // Milliseconds
	NNG_UNIT_EVENTS   = 4  // Some other type of event
};

// nng_stat_value returns the actual value of the statistic.
// Statistic values reflect their value at the time that the corresponding
// snapshot was updated, and are undefined until an update is performed.
NNG_DECL uint64_t nng_stat_value(nng_stat *);

// nng_stat_bool returns the boolean value of the statistic.
NNG_DECL bool nng_stat_bool(nng_stat *);

// nng_stat_string returns the string associated with a string statistic,
// or NULL if the statistic is not part of the string.  The value returned
// is valid until the associated statistic is freed.
NNG_DECL const char *nng_stat_string(nng_stat *);

// nng_stat_desc returns a human-readable description of the statistic.
// This may be useful for display in diagnostic interfaces, etc.
NNG_DECL const char *nng_stat_desc(nng_stat *);

// nng_stat_timestamp returns a timestamp (milliseconds) when the statistic
// was captured.  The base offset is the same as used by nng_clock().
// We don't use nng_time though, because that's in the supplemental header.
NNG_DECL uint64_t nng_stat_timestamp(nng_stat *);

// Device functionality.  This connects two sockets together in a device,
// which means that messages from one side are forwarded to the other.
// This version is synchronous, which means the caller will block until
// one of the sockets is closed. Note that caller is responsible for
// finally closing both sockets when this function returns.
NNG_DECL int nng_device(nng_socket, nng_socket);

// Asynchronous form of nng_device.  When this succeeds, the device is
// left intact and functioning in the background, until one of the sockets
// is closed or the application exits.  The sockets may be shut down if
// the device fails, but the caller is responsible for ultimately closing
// the sockets properly after the device is torn down.
NNG_DECL void nng_device_aio(nng_aio *, nng_socket, nng_socket);

// Symbol name and visibility.  TBD.  The only symbols that really should
// be directly exported to runtimes IMO are the option symbols.  And frankly
// they have enough special logic around them that it might be best not to
// automate the promotion of them to other APIs.  This is an area open
// for discussion.

// Error codes.  These generally have different values from UNIX errnos,
// so take care about converting them.  The one exception is that 0 is
// unambiguously "success".
//
// NNG_SYSERR is a special code, which allows us to wrap errors from the
// underlying operating system.  We generally prefer to map errors to one
// of the above, but if we cannot, then we just encode an error this way.
// The bit is large enough to accommodate all known UNIX and Win32 error
// codes.  We try hard to match things semantically to one of our standard
// errors.  For example, a connection reset or aborted we treat as a
// closed connection, because that's basically what it means.  (The remote
// peer closed the connection.)  For certain kinds of resource exhaustion
// we treat it the same as memory.  But for files, etc. that's OS-specific,
// and we use the generic below.  Some of the above error codes we use
// internally, and the application should never see (e.g. NNG_EINTR).
//
// NNG_ETRANERR is like ESYSERR, but is used to wrap transport specific
// errors, from different transports.  It should only be used when none
// of the other options are available.

enum nng_errno_enum {
	NNG_EINTR        = 1,
	NNG_ENOMEM       = 2,
	NNG_EINVAL       = 3,
	NNG_EBUSY        = 4,
	NNG_ETIMEDOUT    = 5,
	NNG_ECONNREFUSED = 6,
	NNG_ECLOSED      = 7,
	NNG_EAGAIN       = 8,
	NNG_ENOTSUP      = 9,
	NNG_EADDRINUSE   = 10,
	NNG_ESTATE       = 11,
	NNG_ENOENT       = 12,
	NNG_EPROTO       = 13,
	NNG_EUNREACHABLE = 14,
	NNG_EADDRINVAL   = 15,
	NNG_EPERM        = 16,
	NNG_EMSGSIZE     = 17,
	NNG_ECONNABORTED = 18,
	NNG_ECONNRESET   = 19,
	NNG_ECANCELED    = 20,
	NNG_ENOFILES     = 21,
	NNG_ENOSPC       = 22,
	NNG_EEXIST       = 23,
	NNG_EREADONLY    = 24,
	NNG_EWRITEONLY   = 25,
	NNG_ECRYPTO      = 26,
	NNG_EPEERAUTH    = 27,
	NNG_ENOARG       = 28,
	NNG_EAMBIGUOUS   = 29,
	NNG_EBADTYPE     = 30,
	NNG_ECONNSHUT    = 31,
	NNG_EINTERNAL    = 1000,
	NNG_ESYSERR      = 0x10000000,
	NNG_ETRANERR     = 0x20000000
};

// URL support.  We frequently want to process a URL, and these methods
// give us a convenient way of doing so.

typedef struct nng_url {
	char *u_rawurl;   // never NULL
	char *u_scheme;   // never NULL
	char *u_userinfo; // will be NULL if not specified
	char *u_host;     // including colon and port
	char *u_hostname; // name only, will be "" if not specified
	char *u_port;     // port, will be "" if not specified
	char *u_path;     // path, will be "" if not specified
	char *u_query;    // without '?', will be NULL if not specified
	char *u_fragment; // without '#', will be NULL if not specified
	char *u_requri;   // includes query and fragment, "" if not specified
} nng_url;

// nng_url_parse parses a URL string into a structured form.
// Note that the u_port member will be filled out with a numeric
// port if one isn't specified and a default port is appropriate for
// the scheme.  The URL structure is allocated, along with individual
// members.  It can be freed with nng_url_free.
NNG_DECL int nng_url_parse(nng_url **, const char *);

// nng_url_free frees a URL structure that was created by nng_url_parse().
NNG_DECL void nng_url_free(nng_url *);

// nng_url_clone clones a URL structure.
NNG_DECL int nng_url_clone(nng_url **, const nng_url *);

// nng_version returns the library version as a human readable string.
NNG_DECL const char *nng_version(void);

// nng_stream operations permit direct access to low level streams,
// which can have a variety of uses.  Internally most of the transports
// are built on top of these.  Streams are created by other dialers or
// listeners.  The API for creating dialers and listeners varies.

typedef struct nng_stream          nng_stream;
typedef struct nng_stream_dialer   nng_stream_dialer;
typedef struct nng_stream_listener nng_stream_listener;

NNG_DECL void nng_stream_free(nng_stream *);
NNG_DECL void nng_stream_close(nng_stream *);
NNG_DECL void nng_stream_send(nng_stream *, nng_aio *);
NNG_DECL void nng_stream_recv(nng_stream *, nng_aio *);
NNG_DECL int  nng_stream_get(nng_stream *, const char *, void *, size_t *);
NNG_DECL int  nng_stream_get_bool(nng_stream *, const char *, bool *);
NNG_DECL int  nng_stream_get_int(nng_stream *, const char *, int *);
NNG_DECL int  nng_stream_get_ms(nng_stream *, const char *, nng_duration *);
NNG_DECL int  nng_stream_get_size(nng_stream *, const char *, size_t *);
NNG_DECL int  nng_stream_get_uint64(nng_stream *, const char *, uint64_t *);
NNG_DECL int  nng_stream_get_string(nng_stream *, const char *, char **);
NNG_DECL int  nng_stream_get_ptr(nng_stream *, const char *, void **);
NNG_DECL int  nng_stream_get_addr(nng_stream *, const char *, nng_sockaddr *);
NNG_DECL int  nng_stream_set(nng_stream *, const char *, const void *, size_t);
NNG_DECL int  nng_stream_set_bool(nng_stream *, const char *, bool);
NNG_DECL int  nng_stream_set_int(nng_stream *, const char *, int);
NNG_DECL int  nng_stream_set_ms(nng_stream *, const char *, nng_duration);
NNG_DECL int  nng_stream_set_size(nng_stream *, const char *, size_t);
NNG_DECL int  nng_stream_set_uint64(nng_stream *, const char *, uint64_t);
NNG_DECL int  nng_stream_set_string(nng_stream *, const char *, const char *);
NNG_DECL int  nng_stream_set_ptr(nng_stream *, const char *, void *);

NNG_DECL int nng_stream_dialer_alloc(nng_stream_dialer **, const char *);
NNG_DECL int nng_stream_dialer_alloc_url(
    nng_stream_dialer **, const nng_url *);
NNG_DECL void nng_stream_dialer_free(nng_stream_dialer *);
NNG_DECL void nng_stream_dialer_close(nng_stream_dialer *);
NNG_DECL void nng_stream_dialer_dial(nng_stream_dialer *, nng_aio *);
NNG_DECL int  nng_stream_dialer_set(
     nng_stream_dialer *, const char *, const void *, size_t);
NNG_DECL int nng_stream_dialer_get(
    nng_stream_dialer *, const char *, void *, size_t *);
NNG_DECL int nng_stream_dialer_get_bool(
    nng_stream_dialer *, const char *, bool *);
NNG_DECL int nng_stream_dialer_get_int(
    nng_stream_dialer *, const char *, int *);
NNG_DECL int nng_stream_dialer_get_ms(
    nng_stream_dialer *, const char *, nng_duration *);
NNG_DECL int nng_stream_dialer_get_size(
    nng_stream_dialer *, const char *, size_t *);
NNG_DECL int nng_stream_dialer_get_uint64(
    nng_stream_dialer *, const char *, uint64_t *);
NNG_DECL int nng_stream_dialer_get_string(
    nng_stream_dialer *, const char *, char **);
NNG_DECL int nng_stream_dialer_get_ptr(
    nng_stream_dialer *, const char *, void **);
NNG_DECL int nng_stream_dialer_get_addr(
    nng_stream_dialer *, const char *, nng_sockaddr *);
NNG_DECL int nng_stream_dialer_set_bool(
    nng_stream_dialer *, const char *, bool);
NNG_DECL int nng_stream_dialer_set_int(nng_stream_dialer *, const char *, int);
NNG_DECL int nng_stream_dialer_set_ms(
    nng_stream_dialer *, const char *, nng_duration);
NNG_DECL int nng_stream_dialer_set_size(
    nng_stream_dialer *, const char *, size_t);
NNG_DECL int nng_stream_dialer_set_uint64(
    nng_stream_dialer *, const char *, uint64_t);
NNG_DECL int nng_stream_dialer_set_string(
    nng_stream_dialer *, const char *, const char *);
NNG_DECL int nng_stream_dialer_set_ptr(
    nng_stream_dialer *, const char *, void *);
NNG_DECL int nng_stream_dialer_set_addr(
    nng_stream_dialer *, const char *, const nng_sockaddr *);

NNG_DECL int nng_stream_listener_alloc(nng_stream_listener **, const char *);
NNG_DECL int nng_stream_listener_alloc_url(
    nng_stream_listener **, const nng_url *);
NNG_DECL void nng_stream_listener_free(nng_stream_listener *);
NNG_DECL void nng_stream_listener_close(nng_stream_listener *);
NNG_DECL int  nng_stream_listener_listen(nng_stream_listener *);
NNG_DECL void nng_stream_listener_accept(nng_stream_listener *, nng_aio *);
NNG_DECL int  nng_stream_listener_set(
     nng_stream_listener *, const char *, const void *, size_t);
NNG_DECL int nng_stream_listener_get(
    nng_stream_listener *, const char *, void *, size_t *);
NNG_DECL int nng_stream_listener_get_bool(
    nng_stream_listener *, const char *, bool *);
NNG_DECL int nng_stream_listener_get_int(
    nng_stream_listener *, const char *, int *);
NNG_DECL int nng_stream_listener_get_ms(
    nng_stream_listener *, const char *, nng_duration *);
NNG_DECL int nng_stream_listener_get_size(
    nng_stream_listener *, const char *, size_t *);
NNG_DECL int nng_stream_listener_get_uint64(
    nng_stream_listener *, const char *, uint64_t *);
NNG_DECL int nng_stream_listener_get_string(
    nng_stream_listener *, const char *, char **);
NNG_DECL int nng_stream_listener_get_ptr(
    nng_stream_listener *, const char *, void **);
NNG_DECL int nng_stream_listener_get_addr(
    nng_stream_listener *, const char *, nng_sockaddr *);
NNG_DECL int nng_stream_listener_set_bool(
    nng_stream_listener *, const char *, bool);
NNG_DECL int nng_stream_listener_set_int(
    nng_stream_listener *, const char *, int);
NNG_DECL int nng_stream_listener_set_ms(
    nng_stream_listener *, const char *, nng_duration);
NNG_DECL int nng_stream_listener_set_size(
    nng_stream_listener *, const char *, size_t);
NNG_DECL int nng_stream_listener_set_uint64(
    nng_stream_listener *, const char *, uint64_t);
NNG_DECL int nng_stream_listener_set_string(
    nng_stream_listener *, const char *, const char *);
NNG_DECL int nng_stream_listener_set_ptr(
    nng_stream_listener *, const char *, void *);
NNG_DECL int nng_stream_listener_set_addr(
    nng_stream_listener *, const char *, const nng_sockaddr *);

typedef struct nng_lmq nng_lmq;

NNG_DECL int nng_lmq_alloc(nng_lmq **, size_t );
NNG_DECL void nng_lmq_free(nng_lmq *);
NNG_DECL void nng_lmq_flush(nng_lmq *);
NNG_DECL size_t nng_lmq_len(nng_lmq *);
NNG_DECL size_t nng_lmq_cap(nng_lmq *);
NNG_DECL int nng_lmq_put(nng_lmq *, nng_msg *);
NNG_DECL int nng_lmq_get(nng_lmq *, nng_msg **);
NNG_DECL int nng_lmq_resize(nng_lmq *, size_t );
NNG_DECL bool nng_lmq_full(nng_lmq *);
NNG_DECL bool nng_lmq_empty(nng_lmq *);

// typedef struct nng_id_map nng_id_map;

// NNG_DECL void nng_id_map_init(
//     nng_id_map *, uint64_t , uint64_t , bool );
// NNG_DECL void  nng_id_map_fini(nng_id_map *);
// NNG_DECL void *nng_id_get(nng_id_map *, uint64_t );
// NNG_DECL int   nng_id_set(nng_id_map *, uint64_t , void *);
// NNG_DECL int   nng_id_alloc(nng_id_map *, uint64_t *, void *);
// NNG_DECL int   nng_id_remove(nng_id_map *, uint64_t );

// NANOMQ MQTT variables & APIs
typedef struct conn_param        conn_param;
typedef struct pub_packet_struct pub_packet_struct;
typedef struct pipe_db           nano_pipe_db;

NNG_DECL void *nng_hocon_parse_str(char *str, size_t len);
NNG_DECL int nng_access(const char* name, int flag);

// NANOMQ MQTT API ends
// UDP operations.  These are provided for convenience,
// and should be considered somewhat experimental.

// nng_udp represents a socket / file descriptor for use with UDP
typedef struct nng_udp nng_udp;

// nng_udp_open initializes a UDP socket.  The socket is bound
// to the specified address.
NNG_DECL int nng_udp_open(nng_udp **udpp, nng_sockaddr *sa);

// nng_udp_close closes the underlying UDP socket.
NNG_DECL void nng_udp_close(nng_udp *udp);

// nng_udp_sockname determines the locally bound address.
// This is useful to determine a chosen port after binding to port 0.
NNG_DECL int nng_udp_sockname(nng_udp *udp, nng_sockaddr *sa);

// nng_udp_send sends the data in the aio to the the
// destination specified in the nng_aio.  The iovs are the UDP payload.
// The destination address is the first input (0th) for the aio.
NNG_DECL void nng_udp_send(nng_udp *udp, nng_aio *aio);

// nng_udp_recv receives a message, storing it in the iovs
// from the UDP payload.  If the UDP payload will not fit, then
// NNG_EMSGSIZE results.  The senders address is stored in the
// socket address (nng_sockaddr), which should have been specified
// in the aio's first input.
NNG_DECL void nng_udp_recv(nng_udp *udp, nng_aio *aio);

// nng_udp_membership provides for joining or leaving multicast groups.
NNG_DECL int nng_udp_multicast_membership(
    nng_udp *udp, nng_sockaddr *sa, bool join);

#ifndef NNG_ELIDE_DEPRECATED
// These are legacy APIs that have been deprecated.
// Their use is strongly discouraged.

// nng_msg_getopt is defunct, and should not be used by programs. It
// always returns NNG_ENOTSUP.
NNG_DECL int nng_msg_getopt(nng_msg *, int, void *, size_t *) NNG_DEPRECATED;

// Socket options.  Use nng_socket_get and nng_socket_set instead.
NNG_DECL int nng_getopt(
    nng_socket, const char *, void *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_getopt_bool(nng_socket, const char *, bool *) NNG_DEPRECATED;
NNG_DECL int nng_getopt_int(nng_socket, const char *, int *) NNG_DEPRECATED;
NNG_DECL int nng_getopt_ms(
    nng_socket, const char *, nng_duration *) NNG_DEPRECATED;
NNG_DECL int nng_getopt_size(
    nng_socket, const char *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_getopt_uint64(
    nng_socket, const char *, uint64_t *) NNG_DEPRECATED;
NNG_DECL int nng_getopt_ptr(nng_socket, const char *, void **) NNG_DEPRECATED;
NNG_DECL int nng_getopt_string(
    nng_socket, const char *, char **) NNG_DEPRECATED;
NNG_DECL int nng_setopt(
    nng_socket, const char *, const void *, size_t) NNG_DEPRECATED;
NNG_DECL int nng_setopt_bool(nng_socket, const char *, bool) NNG_DEPRECATED;
NNG_DECL int nng_setopt_int(nng_socket, const char *, int) NNG_DEPRECATED;
NNG_DECL int nng_setopt_ms(
    nng_socket, const char *, nng_duration) NNG_DEPRECATED;
NNG_DECL int nng_setopt_size(nng_socket, const char *, size_t) NNG_DEPRECATED;
NNG_DECL int nng_setopt_uint64(
    nng_socket, const char *, uint64_t) NNG_DEPRECATED;
NNG_DECL int nng_setopt_string(
    nng_socket, const char *, const char *) NNG_DEPRECATED;
NNG_DECL int nng_setopt_ptr(nng_socket, const char *, void *) NNG_DEPRECATED;

// Context options.  Use nng_ctx_get and nng_ctx_set instead.
NNG_DECL int nng_ctx_getopt(
    nng_ctx, const char *, void *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_ctx_getopt_bool(nng_ctx, const char *, bool *) NNG_DEPRECATED;
NNG_DECL int nng_ctx_getopt_int(nng_ctx, const char *, int *) NNG_DEPRECATED;
NNG_DECL int nng_ctx_getopt_ms(
    nng_ctx, const char *, nng_duration *) NNG_DEPRECATED;
NNG_DECL int nng_ctx_getopt_size(
    nng_ctx, const char *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_ctx_setopt(
    nng_ctx, const char *, const void *, size_t) NNG_DEPRECATED;
NNG_DECL int nng_ctx_setopt_bool(nng_ctx, const char *, bool) NNG_DEPRECATED;
NNG_DECL int nng_ctx_setopt_int(nng_ctx, const char *, int) NNG_DEPRECATED;
NNG_DECL int nng_ctx_setopt_ms(
    nng_ctx, const char *, nng_duration) NNG_DEPRECATED;
NNG_DECL int nng_ctx_setopt_size(nng_ctx, const char *, size_t) NNG_DEPRECATED;

// Dialer options.  Use nng_dialer_get and nng_dialer_set instead.
NNG_DECL int nng_dialer_getopt(
    nng_dialer, const char *, void *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_bool(
    nng_dialer, const char *, bool *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_int(
    nng_dialer, const char *, int *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_ms(
    nng_dialer, const char *, nng_duration *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_size(
    nng_dialer, const char *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_sockaddr(
    nng_dialer, const char *, nng_sockaddr *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_uint64(
    nng_dialer, const char *, uint64_t *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_ptr(
    nng_dialer, const char *, void **) NNG_DEPRECATED;
NNG_DECL int nng_dialer_getopt_string(
    nng_dialer, const char *, char **) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt(
    nng_dialer, const char *, const void *, size_t) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt_bool(
    nng_dialer, const char *, bool) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt_int(
    nng_dialer, const char *, int) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt_ms(
    nng_dialer, const char *, nng_duration) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt_size(
    nng_dialer, const char *, size_t) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt_uint64(
    nng_dialer, const char *, uint64_t) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt_ptr(
    nng_dialer, const char *, void *) NNG_DEPRECATED;
NNG_DECL int nng_dialer_setopt_string(
    nng_dialer, const char *, const char *) NNG_DEPRECATED;

// Listener options.  Use nng_listener_get and nng_listener_set instead.
NNG_DECL int nng_listener_getopt(
    nng_listener, const char *, void *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_bool(
    nng_listener, const char *, bool *) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_int(
    nng_listener, const char *, int *) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_ms(
    nng_listener, const char *, nng_duration *) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_size(
    nng_listener, const char *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_sockaddr(
    nng_listener, const char *, nng_sockaddr *) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_uint64(
    nng_listener, const char *, uint64_t *) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_ptr(
    nng_listener, const char *, void **) NNG_DEPRECATED;
NNG_DECL int nng_listener_getopt_string(
    nng_listener, const char *, char **) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt(
    nng_listener, const char *, const void *, size_t) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt_bool(
    nng_listener, const char *, bool) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt_int(
    nng_listener, const char *, int) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt_ms(
    nng_listener, const char *, nng_duration) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt_size(
    nng_listener, const char *, size_t) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt_uint64(
    nng_listener, const char *, uint64_t) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt_ptr(
    nng_listener, const char *, void *) NNG_DEPRECATED;
NNG_DECL int nng_listener_setopt_string(
    nng_listener, const char *, const char *) NNG_DEPRECATED;

// Pipe options.  Use nng_pipe_get instead.
NNG_DECL int nng_pipe_getopt(
    nng_pipe, const char *, void *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_bool(
    nng_pipe, const char *, bool *) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_int(nng_pipe, const char *, int *) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_ms(
    nng_pipe, const char *, nng_duration *) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_size(
    nng_pipe, const char *, size_t *) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_sockaddr(
    nng_pipe, const char *, nng_sockaddr *) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_uint64(
    nng_pipe, const char *, uint64_t *) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_ptr(
    nng_pipe, const char *, void **) NNG_DEPRECATED;
NNG_DECL int nng_pipe_getopt_string(
    nng_pipe, const char *, char **) NNG_DEPRECATED;

// nng_closeall closes all open sockets. Do not call this from
// a library; it will affect all sockets.
NNG_DECL void nng_closeall(void) NNG_DEPRECATED;

// THese functions are deprecated, but they really serve no useful purpose.
NNG_DECL int nng_stream_set_addr(
    nng_stream *, const char *, const nng_sockaddr *) NNG_DEPRECATED;
NNG_DECL int nng_ctx_get_addr(
    nng_ctx, const char *, nng_sockaddr *) NNG_DEPRECATED;
NNG_DECL int nng_ctx_set_addr(
    nng_ctx, const char *, const nng_sockaddr *) NNG_DEPRECATED;

#endif // NNG_ELIDE_DEPRECATED

// nng_init_parameter is used by applications to change a tunable setting.
// This function must be called before any other NNG function for the setting
// to have any effect.  This function is also not thread-safe!
//
// The list of parameters supported is *not* documented, and subject to change.
//
// We try to provide sane defaults, so the use here is intended to provide
// more control for applications that cannot use compile-time configuration.
//
// Applications should not depend on this API for correct operation.
//
// This API is intentionally undocumented.
//
// Parameter settings are lost after nng_fini() is called.
typedef int   nng_init_parameter;
NNG_DECL void nng_init_set_parameter(nng_init_parameter, uint64_t);

// The following list of parameters is not part of our API stability promise.
// In particular the set of parameters that are supported, the default values,
// the range of valid values, and semantics associated therein are subject to
// change at any time.  We won't go out of our way to break these, and we will
// try to prevent changes here from breaking working applications, but this is
// on a best effort basis only.
//
// NOTE: When removing a value, please leave the enumeration in place and add
// a suffix _RETIRED ... this will preserve the binary values for binary
// compatibility.
enum {
	NNG_INIT_PARAMETER_NONE = 0, // ensure values start at 1.

	// Fix the number of threads used for tasks (callbacks),
	// Default is 2 threads per core, capped to NNG_INIT_MAX_TASK_THREADS.
	// At least 2 threads will be created in any case.
	NNG_INIT_NUM_TASK_THREADS,

	// Fix the number of threads used for expiration.  Default is one
	// thread per core, capped to NNG_INIT_MAX_EXPIRE_THREADS.  At least
	// one thread will be created.
	NNG_INIT_NUM_EXPIRE_THREADS,

	// Fix the number of poller threads (used for I/O).  Support varies
	// by platform (many platforms only support a single poller thread.)
	NNG_INIT_NUM_POLLER_THREADS,

	// Fix the number of threads used for DNS resolution.  At least one
	// will be used. Default is controlled by NNG_RESOLV_CONCURRENCY
	// compile time variable.
	NNG_INIT_NUM_RESOLVER_THREADS,

	// Limit the number of threads of created for tasks.
	// NNG will always create at least 2 of these in order to prevent
	// deadlocks. Zero means no limit.  Default is determined by
	// NNG_MAX_TASKQ_THREADS compile time variable.
	NNG_INIT_MAX_TASK_THREADS,

	// Limit the number of threads created for expiration.  Zero means no
	// limit. Default is determined by the NNG_MAX_EXPIRE_THREADS compile
	// time variable.
	NNG_INIT_MAX_EXPIRE_THREADS,

	// Limit the number of poller/IO threads created.  Zero means no limit.
	// Default is determined by NNG_MAX_POLLER_THREADS compile time
	// variable.
	NNG_INIT_MAX_POLLER_THREADS,
};

NNG_DECL void    nng_aio_finish_error(nng_aio *aio, int rv);
NNG_DECL void    nng_aio_finish_sync(nng_aio *aio, int rv);
NNG_DECL uint8_t nng_msg_cmd_type(nng_msg *msg);
NNG_DECL uint8_t nng_msg_get_type(nng_msg *msg);
NNG_DECL uint8_t *nng_msg_header_ptr(nng_msg *msg);
NNG_DECL uint8_t *nng_msg_payload_ptr(nng_msg *msg);
NNG_DECL void     nng_msg_set_payload_ptr(nng_msg *msg, uint8_t *ptr);
NNG_DECL void     nng_msg_clone(nng_msg *msg);
NNG_DECL void     nng_msg_set_cmd_type(nng_msg *m, uint8_t cmd);
NNG_DECL nng_msg *nng_msg_unique(nng_msg *m);
NNG_DECL int      nng_make_parent_dirs(const char *name);
NNG_DECL int      nng_file_put(const char *name, const void *data, size_t sz);
NNG_DECL int      nng_file_get(const char *name, void **datap, size_t *szp);
NNG_DECL bool     nng_file_is_dir(const char *path);
NNG_DECL int      nng_file_delete(const char *name);
NNG_DECL void     nng_msg_set_timestamp(nng_msg *m, uint64_t time);
NNG_DECL uint64_t nng_msg_get_timestamp(nng_msg *m);

NNG_DECL void *nng_msg_get_conn_param(nng_msg *msg);
NNG_DECL void  nng_msg_set_conn_param(nng_msg *msg, void *ptr);

NNG_DECL const uint8_t *conn_param_get_clientid(conn_param *cparam);
NNG_DECL const uint8_t *conn_param_get_pro_name(conn_param *cparam);
NNG_DECL const void *   conn_param_get_will_topic(conn_param *cparam);
NNG_DECL const void *   conn_param_get_will_msg(conn_param *cparam);
NNG_DECL const uint8_t *conn_param_get_username(conn_param *cparam);
NNG_DECL const uint8_t *conn_param_get_password(conn_param *cparam);
NNG_DECL uint8_t        conn_param_get_con_flag(conn_param *cparam);
NNG_DECL uint8_t        conn_param_get_clean_start(conn_param *cparam);
NNG_DECL uint8_t        conn_param_get_will_flag(conn_param *cparam);
NNG_DECL uint8_t        conn_param_get_will_qos(conn_param *cparam);
NNG_DECL uint8_t        conn_param_get_will_retain(conn_param *cparam);
NNG_DECL uint16_t       conn_param_get_keepalive(conn_param *cparam);
NNG_DECL uint8_t        conn_param_get_protover(conn_param *cparam);
NNG_DECL void          *conn_param_get_qos_db(conn_param *cparam);
NNG_DECL void          *conn_param_get_ip_addr_v4(conn_param *cparam);
NNG_DECL void          *conn_param_get_property(conn_param *cparam);
NNG_DECL void          *conn_param_get_will_property(conn_param *cparam);
NNG_DECL void           conn_param_set_qos_db(conn_param *cparam, void *);
NNG_DECL void           conn_param_set_clientid(
              conn_param *cparam, const char *clientid);
NNG_DECL void           conn_param_set_username(
              conn_param *cparam, const char *username);
NNG_DECL void           conn_param_set_password(
              conn_param *cparam, const char *password);
NNG_DECL void        conn_param_set_proto_ver(conn_param *cparam, uint8_t ver);
NNG_DECL uint64_t    conn_param_get_will_delay_timestamp(conn_param *cparam);
NNG_DECL uint64_t    conn_param_get_will_mexp(conn_param *cparam);
NNG_DECL void        nng_msg_set_proto_data(nng_msg *m, void *ops, void *data);
NNG_DECL void       *nng_msg_get_proto_data(nng_msg *m);
NNG_DECL conn_param *nng_pipe_cparam(nng_pipe p);
NNG_DECL bool        nng_pipe_status(nng_pipe p);
NNG_DECL int 		 nng_dialer_off(nng_dialer did);

NNG_DECL void nng_taskq_setter(int num_taskq_threads, int max_taskq_threads);

#if defined(NNG_SUPP_SQLITE)

NNG_DECL int nng_mqtt_qos_db_set_retain(
    void *, const char *, nng_msg *, uint8_t);
NNG_DECL nng_msg *nng_mqtt_qos_db_get_retain(void *, const char *);
NNG_DECL int      nng_mqtt_qos_db_remove_retain(void *, const char *);
NNG_DECL nng_msg **nng_mqtt_qos_db_find_retain(void *, const char *);

#endif


// Return an absolute time from some arbitrary point.  The value is
// provided in milliseconds, and is of limited resolution based on the
// system clock.  (Do not use it for fine-grained performance measurements.)
NNG_DECL nng_time nng_clock(void);

// Sleep for specified msecs.
NNG_DECL void nng_msleep(nng_duration);

// nng_random returns a "strong" (cryptographic sense) random number.
NNG_DECL uint32_t nng_random(void);

// nng_socket_pair is used to create a bound pair of file descriptors
// typically using the socketpair() call.  The descriptors are backed
// by reliable, bidirectional, byte streams.  This will return NNG_ENOTSUP
// if the platform lacks support for this.  The argument is a pointer
// to an array of file descriptors (or HANDLES or similar).
NNG_DECL int nng_socket_pair(int[2]);

// Multithreading and synchronization functions.

// nng_thread is a handle to a "thread", which may be a real system
// thread, or a coroutine on some platforms.
typedef struct nng_thread nng_thread;

// Create and start a thread.  Note that on some platforms, this might
// actually be a coroutine, with limitations about what system APIs
// you can call.  Therefore, these threads should only be used with the
// I/O APIs provided by nng.  The thread runs until completion.
NNG_DECL int nng_thread_create(nng_thread **, void (*)(void *), void *);

// Set the thread name.  Support for this is platform specific and varies.
// It is intended to provide information for use when debugging applications,
// and not for programmatic use beyond that.
NNG_DECL void nng_thread_set_name(nng_thread *, const char *);

// Destroy a thread (waiting for it to complete.)  When this function
// returns all resources for the thread are cleaned up.
NNG_DECL void nng_thread_destroy(nng_thread *);

// nng_mtx represents a mutex, which is a simple, non-reentrant, boolean lock.
typedef struct nng_mtx nng_mtx;

// nng_mtx_alloc allocates a mutex structure.
NNG_DECL int nng_mtx_alloc(nng_mtx **);

// nng_mtx_free frees the mutex.  It must not be locked.
NNG_DECL void nng_mtx_free(nng_mtx *);

// nng_mtx_lock locks the mutex; if it is already locked it will block
// until it can be locked.  If the caller already holds the lock, the
// results are undefined (a panic may occur).
NNG_DECL void nng_mtx_lock(nng_mtx *);

// nng_mtx_unlock unlocks a previously locked mutex.  It is an error to
// call this on a mutex which is not owned by caller.
NNG_DECL void nng_mtx_unlock(nng_mtx *);

// nng_cv is a condition variable.  It is always allocated with an
// associated mutex, which must be held when waiting for it, or
// when signaling it.
typedef struct nng_cv nng_cv;

NNG_DECL int nng_cv_alloc(nng_cv **, nng_mtx *);

// nng_cv_free frees the condition variable.
NNG_DECL void nng_cv_free(nng_cv *);

// nng_cv_wait waits until the condition variable is "signaled".
NNG_DECL void nng_cv_wait(nng_cv *);

// nng_cv_until waits until either the condition is signaled, or
// the timeout expires.  It returns NNG_ETIMEDOUT in that case.
NNG_DECL int nng_cv_until(nng_cv *, nng_time);

// nng_cv_wake wakes all threads waiting on the condition.
NNG_DECL void nng_cv_wake(nng_cv *);

// nng_cv_wake1 wakes only one thread waiting on the condition.  This may
// reduce the thundering herd problem, but care must be taken to ensure
// that no waiter starves forever.
NNG_DECL void nng_cv_wake1(nng_cv *);

// New URL accessors for endpoints - from NNG 2.0.
NNG_DECL int nng_dialer_get_url(nng_dialer, const nng_url **);
NNG_DECL int nng_listener_get_url(nng_listener, const nng_url **);

#ifdef __cplusplus
}
#endif

#endif // NNG_NNG_H
