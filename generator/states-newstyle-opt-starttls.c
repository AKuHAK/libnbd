/* nbd client library in userspace: state machine
 * Copyright (C) 2013-2019 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* State machine for sending NBD_OPT_STARTTLS. */

/* STATE MACHINE */ {
 NEWSTYLE.OPT_STARTTLS.START:
  conn->sbuf.option.version = htobe64 (NBD_NEW_VERSION);
  conn->sbuf.option.option = htobe32 (NBD_OPT_STARTTLS);
  conn->sbuf.option.optlen = 0;
  conn->wbuf = &conn->sbuf;
  conn->wlen = sizeof (conn->sbuf.option);
  SET_NEXT_STATE (%SEND);
  return 0;

 NEWSTYLE.OPT_STARTTLS.SEND:
  switch (send_from_wbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    conn->rbuf = &conn->sbuf;
    conn->rlen = sizeof (conn->sbuf.or.option_reply);
    SET_NEXT_STATE (%RECV_REPLY);
  }
  return 0;

 NEWSTYLE.OPT_STARTTLS.RECV_REPLY:
  switch (recv_into_rbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0: SET_NEXT_STATE (%CHECK_REPLY);
  }
  return 0;

 NEWSTYLE.OPT_STARTTLS.CHECK_REPLY:
  uint64_t magic;
  uint32_t option;
  uint32_t reply;
  uint32_t len;
  struct socket *new_sock;

  magic = be64toh (conn->sbuf.or.option_reply.magic);
  option = be32toh (conn->sbuf.or.option_reply.option);
  reply = be32toh (conn->sbuf.or.option_reply.reply);
  len = be32toh (conn->sbuf.or.option_reply.replylen);
  if (magic != NBD_REP_MAGIC || option != NBD_OPT_STARTTLS || len != 0) {
    SET_NEXT_STATE (%.DEAD);
    set_error (0, "handshake: invalid option reply magic, option or length");
    return -1;
  }
  switch (reply) {
  case NBD_REP_ACK:
    new_sock = nbd_internal_crypto_create_session (conn, conn->sock);
    if (new_sock == NULL) {
      SET_NEXT_STATE (%.DEAD);
      return -1;
    }
    conn->sock = new_sock;
    if (nbd_internal_crypto_is_reading (conn))
      SET_NEXT_STATE (%TLS_HANDSHAKE_READ);
    else
      SET_NEXT_STATE (%TLS_HANDSHAKE_WRITE);
    return 0;

  default:
    if (!NBD_REP_IS_ERR (reply))
      debug (conn->h,
             "server is confused by NBD_OPT_STARTTLS, continuing anyway");

    /* Server refused to upgrade to TLS.  If h->tls is not require (2)
     * then we can continue unencrypted.
     */
    if (h->tls == 2) {
      SET_NEXT_STATE (%.DEAD);
      set_error (ENOTSUP, "handshake: server refused TLS, "
                 "but handle TLS setting is require (2)");
      return -1;
    }

    debug (conn->h,
           "server refused TLS (%s), continuing with unencrypted connection",
           reply == NBD_REP_ERR_POLICY ? "policy" : "not supported");
    SET_NEXT_STATE (%^OPT_STRUCTURED_REPLY.START);
    return 0;
  }
  return 0;

 NEWSTYLE.OPT_STARTTLS.TLS_HANDSHAKE_READ:
  int r;

  r = nbd_internal_crypto_handshake (conn);
  if (r == -1) {
    SET_NEXT_STATE (%.DEAD);
    return -1;
  }
  if (r == 0) {
    /* Finished handshake. */
    nbd_internal_crypto_debug_tls_enabled (conn);

    /* Continue with option negotiation. */
    SET_NEXT_STATE (%^OPT_STRUCTURED_REPLY.START);
    return 0;
  }
  /* Continue handshake. */
  if (nbd_internal_crypto_is_reading (conn))
    SET_NEXT_STATE (%TLS_HANDSHAKE_READ);
  else
    SET_NEXT_STATE (%TLS_HANDSHAKE_WRITE);
  return 0;

 NEWSTYLE.OPT_STARTTLS.TLS_HANDSHAKE_WRITE:
  int r;

  r = nbd_internal_crypto_handshake (conn);
  if (r == -1) {
    SET_NEXT_STATE (%.DEAD);
    return -1;
  }
  if (r == 0) {
    /* Finished handshake. */
    debug (conn->h, "connection is using TLS");

    /* Continue with option negotiation. */
    SET_NEXT_STATE (%^OPT_STRUCTURED_REPLY.START);
    return 0;
  }
  /* Continue handshake. */
  if (nbd_internal_crypto_is_reading (conn))
    SET_NEXT_STATE (%TLS_HANDSHAKE_READ);
  else
    SET_NEXT_STATE (%TLS_HANDSHAKE_WRITE);
  return 0;

} /* END STATE MACHINE */