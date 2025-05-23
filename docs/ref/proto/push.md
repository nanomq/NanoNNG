# PUSH protocol

## DESCRIPTION

The {{i:*PUSH* protocol}}{{hi:*PUSH*}} is one half of a {{i:pipeline pattern}}.
The other side is the [_PULL_][pull] protocol.

In the pipeline pattern, pushers distribute messages to pullers.
Each message sent by a pusher will be sent to one of its peer pullers,
chosen in a round-robin fashion
from the set of connected peers available for receiving.
This property makes this pattern useful in {{i:load-balancing}} scenarios.

### Socket Operations

The [`nng_push0_open`][nng_push_open] call creates a _PUSH_ socket.
This socket may be used to send messages, but is unable to receive them.
Attempts to receive messages will result in `NNG_ENOTSUP`.

Send operations will observe flow control (back-pressure), so that
only peers capable of accepting a message will be considered.
If no peer is available to receive a message, then the send operation will
wait until one is available, or the operation times out.

> [!NOTE]
> Although the pipeline protocol honors flow control, and attempts
> to avoid dropping messages, no guarantee of delivery is made.
> Furthermore, as there is no capability for message acknowledgment,
> applications that need reliable delivery are encouraged to consider the
> [_REQ_][req] protocol instead.

### Protocol Versions

Only version 0 of this protocol is supported.
(At the time of writing, no other versions of this protocol have been defined.)

### Protocol Options

- [`NNG_OPT_SENDBUF`][NNG_OPT_SENDBUF]:
  (`int`, 0 - 8192)
  Normally this is set to zero, indicating that send operations are unbuffered.
  In unbuffered operation, send operations will wait until a suitable peer is available to receive the message.
  If this is set to a positive value (up to 8192), then an intermediate buffer is
  provided for the socket with the specified depth (in messages).

> [!NOTE]
> Transport layer buffering may occur in addition to any socket
> buffer determined by this option.

### Protocol Headers

The _PUSH_ protocol has no protocol-specific headers.

[nng_push_open]: TODO.md
[NNG_OPT_SENDBUF]: TODO.md
[pull]: ./pull.md
[req]: ./req.md
