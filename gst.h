// Tonu Jaansoo 2017

#ifndef GST_H
#define GST_H

#include <QObject>
#include <memory>

#include "element.h"

class GST : public QObject
{
    Q_OBJECT
public:
    GST();

    void run(int argc, char *argv[]);
    void doFinish();

    //------------------------------------------------------------------------------
private:
    friend gboolean    bus_cb (GstBus *bus,GstMessage *message, gpointer data);
    friend gboolean    cb_retry_rtspclientsink (gpointer data);
    friend gboolean    cb_handle_error(gpointer data);

    void        insert(GstElement * bin, const QByteArray &name, const QByteArray &factoryName );
    void        rm(const QByteArray &name);
    Element &   get(const QByteArray &name);
    void        link(const QByteArray &left, const QByteArray &right);
    void        link(const QByteArray &left, const QByteArray &filt, const QByteArray &right);

    void create_and_link_main_bin();
    void create_and_link_rtsp_bin();
    void link_rtsp_bin_to_main_bin();
    void unlink_rtsp_bin_to_main_bin();

    GstElement *_main_bin;
    GstElement *_rtspclientsink_bin;
    GstBus     *_bus;
    GMainLoop  *_loop;
    guint       _bus_watch_id;

    QMap<QByteArray, std::shared_ptr<Element>> _elements;
};

#endif // GST_H
