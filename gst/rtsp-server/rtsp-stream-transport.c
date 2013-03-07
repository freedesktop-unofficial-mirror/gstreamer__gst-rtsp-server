/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <stdlib.h>

#include "rtsp-stream-transport.h"

#define GST_RTSP_STREAM_TRANSPORT_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_STREAM_TRANSPORT, GstRTSPStreamTransportPrivate))

struct _GstRTSPStreamTransportPrivate
{
  GstRTSPStream *stream;

  GstRTSPSendFunc send_rtp;
  GstRTSPSendFunc send_rtcp;
  gpointer user_data;
  GDestroyNotify notify;

  GstRTSPKeepAliveFunc keep_alive;
  gpointer ka_user_data;
  GDestroyNotify ka_notify;
  gboolean active;
  gboolean timed_out;

  GstRTSPTransport *transport;

  GObject *rtpsource;
};

enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_stream_transport_debug);
#define GST_CAT_DEFAULT rtsp_stream_transport_debug

static void gst_rtsp_stream_transport_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPStreamTransport, gst_rtsp_stream_transport,
    G_TYPE_OBJECT);

static void
gst_rtsp_stream_transport_class_init (GstRTSPStreamTransportClass * klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (GstRTSPStreamTransportPrivate));

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_stream_transport_finalize;

  GST_DEBUG_CATEGORY_INIT (rtsp_stream_transport_debug, "rtspmediatransport",
      0, "GstRTSPStreamTransport");
}

static void
gst_rtsp_stream_transport_init (GstRTSPStreamTransport * trans)
{
  GstRTSPStreamTransportPrivate *priv =
      GST_RTSP_STREAM_TRANSPORT_GET_PRIVATE (trans);

  trans->priv = priv;
}

static void
gst_rtsp_stream_transport_finalize (GObject * obj)
{
  GstRTSPStreamTransportPrivate *priv;
  GstRTSPStreamTransport *trans;

  trans = GST_RTSP_STREAM_TRANSPORT (obj);
  priv = trans->priv;

  /* remove callbacks now */
  gst_rtsp_stream_transport_set_callbacks (trans, NULL, NULL, NULL, NULL);
  gst_rtsp_stream_transport_set_keepalive (trans, NULL, NULL, NULL);

  if (priv->transport)
    gst_rtsp_transport_free (priv->transport);

#if 0
  if (priv->rtpsource)
    g_object_set_qdata (priv->rtpsource, ssrc_stream_map_key, NULL);
#endif

  G_OBJECT_CLASS (gst_rtsp_stream_transport_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_stream_transport_new:
 * @stream: a #GstRTSPStream
 * @tr: (transfer full): a GstRTSPTransport
 *
 * Create a new #GstRTSPStreamTransport that can be used to manage
 * @stream with transport @tr.
 *
 * Returns: a new #GstRTSPStreamTransport
 */
GstRTSPStreamTransport *
gst_rtsp_stream_transport_new (GstRTSPStream * stream, GstRTSPTransport * tr)
{
  GstRTSPStreamTransportPrivate *priv;
  GstRTSPStreamTransport *trans;

  g_return_val_if_fail (GST_IS_RTSP_STREAM (stream), NULL);
  g_return_val_if_fail (tr != NULL, NULL);

  trans = g_object_new (GST_TYPE_RTSP_STREAM_TRANSPORT, NULL);
  priv = trans->priv;
  priv->stream = stream;
  priv->transport = tr;

  return trans;
}

/**
 * gst_rtsp_stream_transport_get_stream:
 * @trans: a #GstRTSPStreamTransport
 *
 * Get the #GstRTSPStream used when constructing @trans.
 *
 * Returns: (transfer none): the stream used when constructing @trans.
 */
GstRTSPStream *
gst_rtsp_stream_transport_get_stream (GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), NULL);

  return trans->priv->stream;
}

/**
 * gst_rtsp_stream_transport_set_callbacks:
 * @trans: a #GstRTSPStreamTransport
 * @send_rtp: (scope notified): a callback called when RTP should be sent
 * @send_rtcp: (scope notified): a callback called when RTCP should be sent
 * @user_data: user data passed to callbacks
 * @notify: called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when data for a stream should be sent
 * to a client. This is usually used when sending RTP/RTCP over TCP.
 */
void
gst_rtsp_stream_transport_set_callbacks (GstRTSPStreamTransport * trans,
    GstRTSPSendFunc send_rtp, GstRTSPSendFunc send_rtcp,
    gpointer user_data, GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->send_rtp = send_rtp;
  priv->send_rtcp = send_rtcp;
  if (priv->notify)
    priv->notify (priv->user_data);
  priv->user_data = user_data;
  priv->notify = notify;
}

/**
 * gst_rtsp_stream_transport_set_keepalive:
 * @trans: a #GstRTSPStreamTransport
 * @keep_alive: a callback called when the receiver is active
 * @user_data: user data passed to callback
 * @notify: called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when RTCP packets are received from the
 * receiver of @trans.
 */
void
gst_rtsp_stream_transport_set_keepalive (GstRTSPStreamTransport * trans,
    GstRTSPKeepAliveFunc keep_alive, gpointer user_data, GDestroyNotify notify)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  priv = trans->priv;

  priv->keep_alive = keep_alive;
  if (priv->ka_notify)
    priv->ka_notify (priv->ka_user_data);
  priv->ka_user_data = user_data;
  priv->ka_notify = notify;
}


/**
 * gst_rtsp_stream_transport_set_transport:
 * @trans: a #GstRTSPStreamTransport
 * @tr: (transfer full): a client #GstRTSPTransport
 *
 * Set @tr as the client transport. This function takes ownership of the
 * passed @tr.
 */
void
gst_rtsp_stream_transport_set_transport (GstRTSPStreamTransport * trans,
    GstRTSPTransport * tr)
{
  GstRTSPStreamTransportPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));
  g_return_if_fail (tr != NULL);

  priv = trans->priv;

  /* keep track of the transports in the stream. */
  if (priv->transport)
    gst_rtsp_transport_free (priv->transport);
  priv->transport = tr;
}

/**
 * gst_rtsp_stream_transport_get_transport:
 * @trans: a #GstRTSPStreamTransport
 *
 * Get the transport configured in @trans.
 *
 * Returns: (transfer none): the transport configured in @trans. It remains
 *     valid for as long as @trans is valid.
 */
const GstRTSPTransport *
gst_rtsp_stream_transport_get_transport (GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), NULL);

  return trans->priv->transport;
}

/**
 * gst_rtsp_stream_transport_set_active:
 * @trans: a #GstRTSPStreamTransport
 * @active: new state of @trans
 *
 * Activate or deactivate datatransfer configured in @trans.
 *
 * Returns: %TRUE when the state was changed.
 */
gboolean
gst_rtsp_stream_transport_set_active (GstRTSPStreamTransport * trans,
    gboolean active)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);

  priv = trans->priv;

  if (priv->active == active)
    return FALSE;

  if (active)
    res = gst_rtsp_stream_add_transport (priv->stream, trans);
  else
    res = gst_rtsp_stream_remove_transport (priv->stream, trans);

  if (res)
    priv->active = active;

  return res;
}

/**
 * gst_rtsp_stream_transport_set_timed_out:
 * @trans: a #GstRTSPStreamTransport
 * @timedout: timed out value
 *
 * Set the timed out state of @trans to @timedout
 */
void
gst_rtsp_stream_transport_set_timed_out (GstRTSPStreamTransport * trans,
    gboolean timedout)
{
  g_return_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans));

  trans->priv->timed_out = timedout;
}

/**
 * gst_rtsp_stream_transport_is_timed_out:
 * @trans: a #GstRTSPStreamTransport
 *
 * Check if @trans is timed out.
 *
 * Returns: %TRUE if @trans timed out.
 */
gboolean
gst_rtsp_stream_transport_is_timed_out (GstRTSPStreamTransport * trans)
{
  g_return_val_if_fail (GST_IS_RTSP_STREAM_TRANSPORT (trans), FALSE);

  return trans->priv->timed_out;
}

/**
 * gst_rtsp_stream_transport_send_rtp:
 * @trans: a #GstRTSPStreamTransport
 * @buffer: a #GstBuffer
 *
 * Send @buffer to the installed RTP callback for @trans.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_rtsp_stream_transport_send_rtp (GstRTSPStreamTransport * trans,
    GstBuffer * buffer)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res = FALSE;

  priv = trans->priv;

  if (priv->send_rtp)
    res =
        priv->send_rtp (buffer, priv->transport->interleaved.min,
        priv->user_data);

  return res;
}

/**
 * gst_rtsp_stream_transport_send_rtcp:
 * @trans: a #GstRTSPStreamTransport
 * @buffer: a #GstBuffer
 *
 * Send @buffer to the installed RTCP callback for @trans.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_rtsp_stream_transport_send_rtcp (GstRTSPStreamTransport * trans,
    GstBuffer * buffer)
{
  GstRTSPStreamTransportPrivate *priv;
  gboolean res = FALSE;

  priv = trans->priv;

  if (priv->send_rtcp)
    res =
        priv->send_rtcp (buffer, priv->transport->interleaved.max,
        priv->user_data);

  return res;
}

/**
 * gst_rtsp_stream_transport_keep_alive:
 * @trans: a #GstRTSPStreamTransport
 *
 * Signal the installed keep_alive callback for @trans.
 */
void
gst_rtsp_stream_transport_keep_alive (GstRTSPStreamTransport * trans)
{
  GstRTSPStreamTransportPrivate *priv;

  priv = trans->priv;

  if (priv->keep_alive)
    priv->keep_alive (priv->ka_user_data);
}