// Tonu Jaansoo 2017

#ifndef ELEMENT_H
#define ELEMENT_H

#include <QByteArray>
#include <QDebug>
#include <gst/gst.h>

class Element
{
public:
    Element()
        :
          _factoryName(""),
          _gstElement(NULL)
    {
    }

    Element(const QByteArray &name, const QByteArray &factoryName)
        :
          _factoryName(factoryName),
          _gstElement(NULL)
    {
        _gstElement = gst_element_factory_make (_factoryName.constData(), name.constData());

        g_assert(_gstElement);
        g_object_ref(_gstElement);

        qDebug() << "Created elem:" << name;
    }

    ~Element()
    {
        g_object_unref(_gstElement);

        qDebug() << "Deleted elem:" << _factoryName;
    }

    template <class T>
    void set(const QByteArray &prop, const T & value)
    {
        qDebug() << "set " << prop << "=" << value;

        g_object_set(G_OBJECT(_gstElement), prop.constData(), value, NULL);
    }

    operator GstElement *  ()
    {
        return _gstElement;
    }

    friend class GST;

private:
    QByteArray _factoryName;
    GstElement *_gstElement;
};

#endif // ELEMENT_H
