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

/* State machine for negotiating NBD_OPT_SET_META_CONTEXT. */

/* STATE MACHINE */ {
 NEWSTYLE.OPT_SET_META_CONTEXT.START:
  size_t i, nr_queries;
  uint32_t len;

  /* If the server doesn't support SRs then we must skip this group.
   * Also we skip the group if the client didn't request any metadata
   * contexts.
   */
  if (!conn->structured_replies ||
      h->request_meta_contexts == NULL ||
      nbd_internal_string_list_length (h->request_meta_contexts) == 0) {
    SET_NEXT_STATE (%FINISH);
    return 0;
  }

  assert (conn->meta_contexts == NULL);

  /* Calculate the length of the option request data. */
  len = 4 /* exportname len */ + strlen (h->export_name) + 4 /* nr queries */;
  nr_queries = nbd_internal_string_list_length (h->request_meta_contexts);
  for (i = 0; i < nr_queries; ++i)
    len += 4 /* length of query */ + strlen (h->request_meta_contexts[i]);

  conn->sbuf.option.version = htobe64 (NBD_NEW_VERSION);
  conn->sbuf.option.option = htobe32 (NBD_OPT_SET_META_CONTEXT);
  conn->sbuf.option.optlen = htobe32 (len);
  conn->wbuf = &conn->sbuf;
  conn->wlen = sizeof (conn->sbuf.option);
  SET_NEXT_STATE (%SEND);
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.SEND:
  switch (send_from_wbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    conn->sbuf.len = htobe32 (strlen (h->export_name));
    conn->wbuf = &conn->sbuf.len;
    conn->wlen = sizeof conn->sbuf.len;
    SET_NEXT_STATE (%SEND_EXPORTNAMELEN);
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.SEND_EXPORTNAMELEN:
  switch (send_from_wbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    conn->wbuf = h->export_name;
    conn->wlen = strlen (h->export_name);
    SET_NEXT_STATE (%SEND_EXPORTNAME);
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.SEND_EXPORTNAME:
  switch (send_from_wbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    conn->sbuf.nrqueries =
      htobe32 (nbd_internal_string_list_length (h->request_meta_contexts));
    conn->wbuf = &conn->sbuf;
    conn->wlen = sizeof conn->sbuf.nrqueries;
    SET_NEXT_STATE (%SEND_NRQUERIES);
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.SEND_NRQUERIES:
  switch (send_from_wbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    conn->querynum = 0;
    SET_NEXT_STATE (%PREPARE_NEXT_QUERY);
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.PREPARE_NEXT_QUERY:
  const char *query = h->request_meta_contexts[conn->querynum];

  if (query == NULL) { /* end of list of requested meta contexts */
    SET_NEXT_STATE (%PREPARE_FOR_REPLY);
    return 0;
  }

  conn->sbuf.len = htobe32 (strlen (query));
  conn->wbuf = &conn->sbuf.len;
  conn->wlen = sizeof conn->sbuf.len;
  SET_NEXT_STATE (%SEND_QUERYLEN);
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.SEND_QUERYLEN:
  const char *query = h->request_meta_contexts[conn->querynum];

  switch (send_from_wbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    conn->wbuf = query;
    conn->wlen = strlen (query);
    SET_NEXT_STATE (%SEND_QUERY);
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.SEND_QUERY:
  switch (send_from_wbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    conn->querynum++;
    SET_NEXT_STATE (%PREPARE_NEXT_QUERY);
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.PREPARE_FOR_REPLY:
  conn->rbuf = &conn->sbuf.or.option_reply;
  conn->rlen = sizeof conn->sbuf.or.option_reply;
  SET_NEXT_STATE (%RECV_REPLY);
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.RECV_REPLY:
  uint64_t magic;
  uint32_t option;
  uint32_t reply;
  uint32_t len;
  const uint32_t maxlen = sizeof conn->sbuf.or.payload.context;

  switch (recv_into_rbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    magic = be64toh (conn->sbuf.or.option_reply.magic);
    option = be32toh (conn->sbuf.or.option_reply.option);
    reply = be32toh (conn->sbuf.or.option_reply.reply);
    len = be32toh (conn->sbuf.or.option_reply.replylen);
    if (magic != NBD_REP_MAGIC || option != NBD_OPT_SET_META_CONTEXT) {
      SET_NEXT_STATE (%.DEAD);
      set_error (0, "handshake: invalid option reply magic or option");
      return -1;
    }
    switch (reply) {
    case NBD_REP_ACK:           /* End of list of replies. */
      if (len != 0) {
	SET_NEXT_STATE (%.DEAD);
	set_error (0, "handshake: invalid option reply length");
	return -1;
      }
      SET_NEXT_STATE (%FINISH);
      break;
    case NBD_REP_META_CONTEXT:  /* A context. */
      /* If it's too long, skip over it. */
      if (len > maxlen)
        conn->rbuf = NULL;
      else
        conn->rbuf = &conn->sbuf.or.payload.context;
      conn->rlen = len;
      SET_NEXT_STATE (%RECV_REPLY_PAYLOAD);
      break;
    default:
      /* Anything else is an error, ignore it */
      debug (conn->h, "handshake: unexpected error from "
             "NBD_OPT_SET_META_CONTEXT (%" PRIu32 ")", reply);
      conn->rbuf = NULL;
      conn->rlen = len;
      SET_NEXT_STATE (%RECV_SKIP_PAYLOAD);
    }
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.RECV_REPLY_PAYLOAD:
  struct meta_context *meta_context;
  uint32_t len;

  switch (recv_into_rbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:
    if (conn->rbuf != NULL) {
      len = be32toh (conn->sbuf.or.option_reply.replylen);

      meta_context = malloc (sizeof *meta_context);
      if (meta_context == NULL) {
        set_error (errno, "malloc");
        SET_NEXT_STATE (%.DEAD);
        return -1;
      }
      /* String payload is not NUL-terminated. */
      meta_context->name = strndup (conn->sbuf.or.payload.context.str, len);
      if (meta_context->name == NULL) {
        set_error (errno, "strdup");
        SET_NEXT_STATE (%.DEAD);
        free (meta_context);
        return -1;
      }
      meta_context->context_id =
        be32toh (conn->sbuf.or.payload.context.context.context_id);
      debug (h, "negotiated %s with context ID %" PRIu32,
             meta_context->name, meta_context->context_id);
      meta_context->next = conn->meta_contexts;
      conn->meta_contexts = meta_context;
    }
    SET_NEXT_STATE (%PREPARE_FOR_REPLY);
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.RECV_SKIP_PAYLOAD:
  switch (recv_into_rbuf (conn)) {
  case -1: SET_NEXT_STATE (%.DEAD); return -1;
  case 0:  SET_NEXT_STATE (%FINISH);
    /* XXX: capture instead of skip server's payload to NBD_REP_ERR*? */
  }
  return 0;

 NEWSTYLE.OPT_SET_META_CONTEXT.FINISH:
  /* Jump to the next option. */
  SET_NEXT_STATE (%^OPT_GO.START);
  return 0;

} /* END STATE MACHINE */