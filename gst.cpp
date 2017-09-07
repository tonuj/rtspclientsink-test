// Tonu Jaansoo 2017

#include "gst.h"

#include <QDebug>
#include <unistd.h>
#include <gst/app/app.h>

/// GStreamer callbacks ----------------------------------------------------------------
gboolean cb_retry_rtspclientsink (gpointer data)
{
    GST *app = static_cast<GST*>(data);

    // if rtsp sub bin is'nt already in main bin, do add
    if (!gst_bin_get_by_name(GST_BIN(app->_main_bin), "rtsp_mybin"))
    {
        app->create_and_link_rtsp_bin();
        app->link_rtsp_bin_to_main_bin();
    }

    return TRUE;
}

gboolean cb_handle_error(gpointer data)
{
    GST *app = static_cast<GST*>(data);

    // remove RTSP bin from main bin
    app->unlink_rtsp_bin_to_main_bin();

    GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(app->_main_bin), GST_STATE_PLAYING);

    Q_ASSERT(ret != GST_STATE_CHANGE_FAILURE);

    return FALSE;
}

gboolean bus_cb(GstBus *bus, GstMessage *message, gpointer    data)
{
    Q_UNUSED(bus);

    GST *app = static_cast<GST*>(data);

    switch (GST_MESSAGE_TYPE (message))
    {
    case   GST_MESSAGE_STATE_CHANGED:
    {
        GstState old_state, new_state;

        gst_message_parse_state_changed (message, &old_state, &new_state, NULL);

        qDebug() << GST_OBJECT_NAME (message->src) << gst_element_state_get_name (old_state) << "=>" <<  gst_element_state_get_name (new_state);

        break;
    }
    case GST_MESSAGE_ERROR:
    {
        GError *err;
        gchar *debug;

        gst_message_parse_error (message, &err, &debug);

        qDebug() << "ERROR:" << err->message;

        g_error_free (err);
        g_free (debug);

        g_timeout_add(2000, cb_handle_error, app);

        break;
    }
    case GST_MESSAGE_EOS:

        g_main_loop_quit(app->_loop);
        break;
    default:
        qDebug() << "Got message" << GST_MESSAGE_TYPE_NAME (message);
        break;
    }

    return TRUE;
}


///  ----------------------------------------------------------------
GST::GST()
{
}

void GST::doFinish()
{
    gst_element_set_state(GST_ELEMENT(_main_bin), GST_STATE_NULL);

    g_main_loop_quit(_loop);
}


void GST::run(int argc, char *argv[])
{
    gst_init (&argc, &argv);

    _main_bin = gst_pipeline_new (NULL);
    Q_ASSERT(_main_bin);

    _bus = gst_pipeline_get_bus (GST_PIPELINE (_main_bin));
    _bus_watch_id = gst_bus_add_watch (_bus, bus_cb, this);
    gst_object_unref (_bus);

    _loop = g_main_loop_new (NULL, FALSE);

    // create fixed pipeline part
    create_and_link_main_bin();

    // RTSP retry timer
    g_timeout_add_seconds(10, cb_retry_rtspclientsink, this);

    // start main bin without rtsp
    gst_element_set_state(GST_ELEMENT(_main_bin), GST_STATE_PLAYING);

    g_main_loop_run (_loop);

    qDebug() << "Quitting...";
}

void GST::create_and_link_main_bin()
{
    ///video source ----------------------------------------------------------------
    insert(_main_bin, "videosrc", "videotestsrc");
    get("videosrc").set("is-live",            TRUE);

    insert(_main_bin, "videotee", "tee");

    link("videosrc", "videotee");

    ///audio source ----------------------------------------------------------------
    insert(_main_bin, "audiosrc",     "audiotestsrc");
    get("audiosrc").set("is-live",            TRUE);

    insert(_main_bin, "audioconvert", "audioconvert");
    insert(_main_bin, "audiotee",     "tee");

    link("audiosrc", "audio/x-raw,format=S16LE,rate=48000,channels=1", "audioconvert");
    link("audioconvert", "audiotee");

    ///TSMUX ----------------------------------------------------------------

    ///audio to tsmux ----------------------------------------------------------------
    insert(_main_bin, "file_audioqueue1",    "queue");

#ifdef Q_PROCESSOR_ARM_V7
    insert(_main_bin, "file_audioenc",       "imxmp3audioenc");
#else
    insert(_main_bin, "file_audioenc",       "lamemp3enc");
#endif
    insert(_main_bin, "file_mpegaudioparse", "mpegaudioparse");
    insert(_main_bin, "file_audioqueue2",    "queue");

    get("file_audioenc").set("bitrate", 160);

    link("file_audioqueue1",   "file_audioenc");
    link("file_audioenc",      "file_mpegaudioparse");
    link("file_mpegaudioparse", "file_audioqueue2");

    /// video to tsmux ----------------------------------------------------------------
    insert(_main_bin, "file_videoqueue1",    "queue");
#ifdef Q_PROCESSOR_ARM_V7
    insert(_main_bin, "file_videoenc",       "imxvpuenc_h264");
#else
    insert(_main_bin, "file_videoenc",       "x264enc");
#endif
    insert(_main_bin, "file_h264videoparse", "h264parse");
    insert(_main_bin, "file_videoqueue2",    "queue");

    get("file_videoenc").set("bitrate", 3000);

    link("file_videoqueue1", "file_videoenc");
    link("file_videoenc",  "video/x-h264,profile=baseline", "file_h264videoparse");
    link("file_h264videoparse", "file_videoqueue2");

    /// Mux to filesink ----------------------------------------------------------------
    insert(_main_bin, "file_tsmux",            "mpegtsmux");
    insert(_main_bin, "file_multifilesink",    "multifilesink");

    get("file_multifilesink").set("location", "video_%04d.ts");
    get("file_multifilesink").set("next-file", 4);
    get("file_multifilesink").set("post-messages", TRUE);

    link("file_tsmux", "file_multifilesink");

    //link the rest
    link("audiotee", "file_audioqueue1");
    link("videotee", "file_videoqueue1");

    link("file_audioqueue2", "file_tsmux");
    link("file_videoqueue2", "file_tsmux");
}

void GST::create_and_link_rtsp_bin()
{
    _rtspclientsink_bin = gst_bin_new ("rtsp_mybin");
    gst_object_ref (GST_OBJECT (_rtspclientsink_bin));

    insert(_rtspclientsink_bin, "rtsp_audioqueue1",    "queue");
    insert(_rtspclientsink_bin, "rtsp_videoqueue1",    "queue");

    ///audio to rtsp ----------------------------------------------------------------
#ifdef Q_PROCESSOR_ARM_V7
    insert(_rtspclientsink_bin, "rtsp_audioenc",       "imxmp3audioenc");
#else
    insert(_rtspclientsink_bin, "rtsp_audioenc",       "lamemp3enc");
#endif
    insert(_rtspclientsink_bin, "rtsp_mpegaudioparse", "mpegaudioparse");
    insert(_rtspclientsink_bin, "rtsp_audioqueue2",    "queue");

    get("rtsp_audioenc").set("bitrate", 96);

    link("rtsp_audioqueue1", "rtsp_audioenc");
    link("rtsp_audioenc",      "rtsp_mpegaudioparse");
    link("rtsp_mpegaudioparse", "rtsp_audioqueue2");

    /// video to rtsp ----------------------------------------------------------------
#ifdef Q_PROCESSOR_ARM_V7
    insert(_rtspclientsink_bin, "rtsp_videoenc",       "imxvpuenc_h264");
#else
    insert(_rtspclientsink_bin, "rtsp_videoenc",       "x264enc");
#endif
    insert(_rtspclientsink_bin, "rtsp_h264videoparse", "h264parse");
    insert(_rtspclientsink_bin, "rtsp_videoqueue2",    "queue");

    get("rtsp_videoenc").set("bitrate", 500);
    get("rtsp_videoenc").set("idr-interval", 60);

    link("rtsp_videoqueue1", "rtsp_videoenc");
    link("rtsp_videoenc", "video/x-h264,profile=baseline", "rtsp_h264videoparse");
    link("rtsp_h264videoparse", "rtsp_videoqueue2");


    /// rtspclientsink ----------------------------------------------------------------
    insert(_rtspclientsink_bin, "rtsp_clientsink",            "rtspclientsink");

    get("rtsp_clientsink").set("location", "rtsp://138.68.128.116:8030/test");
    get("rtsp_clientsink").set("profiles", 4);
    get("rtsp_clientsink").set("latency", 1000);

    /// errignores ----------------------------------------------------------------
    insert(_rtspclientsink_bin, "rtsp_errorignore_vid", "errorignore");
    insert(_rtspclientsink_bin, "rtsp_errorignore_aud", "errorignore");

    link("rtsp_videoqueue2", "rtsp_errorignore_vid");
    link("rtsp_audioqueue2", "rtsp_errorignore_aud");

    link("rtsp_errorignore_vid", "rtsp_clientsink");
    link("rtsp_errorignore_aud", "rtsp_clientsink");


    get("rtsp_errorignore_vid").set("convert-to", 0);
    get("rtsp_errorignore_vid").set("ignore-error", true);
    get("rtsp_errorignore_vid").set("ignore-notlinked", true);
    get("rtsp_errorignore_vid").set("ignore-notnegotiated", true);

    get("rtsp_errorignore_aud").set("convert-to", 0);
    get("rtsp_errorignore_aud").set("ignore-error", true);
    get("rtsp_errorignore_aud").set("ignore-notlinked", true);
    get("rtsp_errorignore_aud").set("ignore-notnegotiated", true);


    /// add ghostpads
    GstPad * audio_pad = gst_element_get_static_pad (get("rtsp_audioqueue1"), "sink");
    GstPad * video_pad = gst_element_get_static_pad (get("rtsp_videoqueue1"), "sink");

    gst_element_add_pad (_rtspclientsink_bin, gst_ghost_pad_new ("sink_audio", audio_pad));
    gst_element_add_pad (_rtspclientsink_bin, gst_ghost_pad_new ("sink_video", video_pad));

    gst_object_unref (GST_OBJECT (audio_pad));
    gst_object_unref (GST_OBJECT (video_pad));
}

void GST::link_rtsp_bin_to_main_bin()
{
    if (!gst_bin_add(GST_BIN(_main_bin), GST_ELEMENT(_rtspclientsink_bin)))
    {
        Q_ASSERT(0);
    }

    GstPad *src_audio_pad = gst_element_get_request_pad (get("audiotee"), "src_%u");
    GstPad *src_video_pad = gst_element_get_request_pad (get("videotee"), "src_%u");

    Q_ASSERT(src_audio_pad);
    Q_ASSERT(src_video_pad);

    GstPad * dst_audio_pad = gst_element_get_static_pad (_rtspclientsink_bin, "sink_audio");
    GstPad * dst_video_pad = gst_element_get_static_pad (_rtspclientsink_bin, "sink_video");

    Q_ASSERT(dst_audio_pad);
    Q_ASSERT(dst_video_pad);

    GstStateChangeReturn ret = gst_element_set_state(_rtspclientsink_bin, GST_STATE_PLAYING);

    Q_ASSERT(ret != GST_STATE_CHANGE_FAILURE);

    if (gst_pad_link (src_audio_pad, dst_audio_pad) != GST_PAD_LINK_OK)
    {
        qDebug() << "Failed to link: audiotee";

        Q_ASSERT(0);
    }

    if (gst_pad_link (src_video_pad, dst_video_pad) != GST_PAD_LINK_OK)
    {
        qDebug() << "Failed to link: videotee";

        Q_ASSERT(0);
    }

    gst_object_unref (GST_OBJECT (src_audio_pad));
    gst_object_unref (GST_OBJECT (src_video_pad));

    gst_object_unref (GST_OBJECT (dst_audio_pad));
    gst_object_unref (GST_OBJECT (dst_video_pad));

}

void GST::unlink_rtsp_bin_to_main_bin()
{
    // unlinking rtsp_mybin
    GstPad * dst_audio_pad = gst_element_get_static_pad (_rtspclientsink_bin, "sink_audio");
    GstPad * dst_video_pad = gst_element_get_static_pad (_rtspclientsink_bin, "sink_video");

    Q_ASSERT(dst_audio_pad);
    Q_ASSERT(dst_video_pad);

    GstPad * src_audio_pad = gst_pad_get_peer(dst_audio_pad);
    GstPad * src_video_pad = gst_pad_get_peer(dst_video_pad);

    Q_ASSERT(src_audio_pad);
    Q_ASSERT(src_video_pad);

    gst_pad_unlink(src_audio_pad, dst_audio_pad);
    gst_pad_unlink(src_video_pad, dst_video_pad);

    g_object_unref(dst_audio_pad);
    g_object_unref(dst_video_pad);
    g_object_unref(src_audio_pad);
    g_object_unref(src_video_pad);

    // setting rtsp_mybin to NULL and remobing from main bin
    GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(_rtspclientsink_bin), GST_STATE_NULL);

    Q_ASSERT(ret != GST_STATE_CHANGE_FAILURE);

    gst_bin_remove(GST_BIN (_main_bin), GST_ELEMENT(_rtspclientsink_bin));

    // all elements will be removed from map and deleted
    _elements.remove("rtsp_audioqueue1");
    _elements.remove("rtsp_videoqueue1");
    _elements.remove("rtsp_audioenc");
    _elements.remove("rtsp_mpegaudioparse");
    _elements.remove("rtsp_audioqueue2");
    _elements.remove("rtsp_videoenc");
    _elements.remove("rtsp_h264videoparse");
    _elements.remove("rtsp_videoqueue2");
    _elements.remove("rtsp_clientsink");
    _elements.remove("rtsp_errorignore_vid");
    _elements.remove("rtsp_errorignore_aud");

    // bin also gets deleted
    g_object_unref(_rtspclientsink_bin);
}


/// helper functions ----------------------------------------------------------------------------------------------------------
void GST::insert(GstElement * bin, const QByteArray &name, const QByteArray &factoryName )
{
    //create GST element if not done so already
    if (!(_elements.contains(name) || factoryName == ""))
    {

        qDebug() << "new elem from factory " << name << "(" << factoryName << ")";
        _elements.insert(name, std::make_shared<Element>(name,factoryName));
    }

    Q_ASSERT(_elements.contains(name));

    //add to the bin
    gst_bin_add (GST_BIN (bin), *_elements[name]);
}

void GST::rm(const QByteArray &name)
{
    Q_ASSERT(_elements.contains(name));

    gst_bin_remove(GST_BIN (_main_bin), get(name));
}


Element & GST::get(const QByteArray &name)
{
    Q_ASSERT(_elements.contains(name));

    return *_elements[name];
}

void GST::link(const QByteArray &left, const QByteArray &right)
{
    Element &l = get(left);
    Element &r = get(right);

    if (!gst_element_link (l, r))
    {
        qDebug() << "Failed to link:" << l._factoryName << "->" << r._factoryName;

        Q_ASSERT(0);
    }

    qDebug() << "Link:" << l._factoryName << "->" << r._factoryName;

}

void GST::link(const QByteArray &left, const QByteArray &filt, const QByteArray &right)
{
    Element &l = get(left);
    Element &r = get(right);

    bool link_ok = false;

    GstCaps *caps = gst_caps_from_string (filt.constData());

    link_ok = gst_element_link_filtered (l._gstElement, r._gstElement, caps);
    gst_caps_unref (caps);

    if (!link_ok)
    {
        qDebug() << "Failed to link:" << l._factoryName << "->\"" << filt << "\"->" << r._factoryName;

        Q_ASSERT(0);
    }

    qDebug() << "Link:" << l._factoryName << "->\"" << filt << "\"->" << r._factoryName;
}
