/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright 2012 The MITRE Corporation                                      *
 *                                                                           *
 * Licensed under the Apache License, Version 2.0 (the "License");           *
 * you may not use this file except in compliance with the License.          *
 * You may obtain a copy of the License at                                   *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 * Unless required by applicable law or agreed to in writing, software       *
 * distributed under the License is distributed on an "AS IS" BASIS,         *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
 * See the License for the specific language governing permissions and       *
 * limitations under the License.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <QRegularExpression>
#include <opencv2/highgui/highgui.hpp>
#include "openbr_internal.h"
#include "openbr/core/opencvutils.h"

using namespace cv;

namespace br
{

/*!
 * \ingroup transforms
 * \brief Applies br::Format to br::Template::file::name and appends results.
 * \author Josh Klontz \cite jklontz
 */
class OpenTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        if (!src.isEmpty()) { dst = src; return; }
        if (Globals->verbose) qDebug("Opening %s", qPrintable(src.file.flat()));
        dst.file = src.file;
        foreach (const File &file, src.file.split()) {
            QScopedPointer<Format> format(Factory<Format>::make(file));
            Template t = format->read();
            if (t.isEmpty()) qWarning("Can't open %s from %s", qPrintable(file.flat()), qPrintable(QDir::currentPath()));
            dst.append(t);
            dst.file.append(t.file.localMetadata());
        }
        dst.file.set("FTO", dst.isEmpty());
    }
};

BR_REGISTER(Transform, OpenTransform)

/*!
 * \ingroup transforms
 * \brief Prints the template's file to stdout or stderr.
 * \author Josh Klontz \cite jklontz
 */
class PrintTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(bool error READ get_error WRITE set_error RESET reset_error)
    Q_PROPERTY(bool data READ get_data WRITE set_data RESET reset_data)
    BR_PROPERTY(bool, error, true)
    BR_PROPERTY(bool, data, false)

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        const QString nameString = src.file.flat();
        const QString dataString = data ? OpenCVUtils::matrixToString(src)+"\n" : QString();
        QStringList matricies;
        foreach (const Mat &m, src)
            matricies.append(QString::number(m.rows) + "x" + QString::number(m.cols) + "_" + OpenCVUtils::typeToString(m));
        fprintf(error ? stderr : stdout, "%s\n  %s\n%s", qPrintable(nameString), qPrintable(matricies.join(",")), qPrintable(dataString));
    }
};

BR_REGISTER(Transform, PrintTransform)

/*!
 * \ingroup transforms
 * \brief Checks the template for NaN values.
 * \author Josh Klontz \cite jklontz
 */
class CheckTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    static int count;
    int index;

 public:
    CheckTransform() : index(count++) {}

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        foreach (const Mat &m, src) {
            Mat fm;
            m.convertTo(fm, CV_32F);
            const int elements = fm.rows * fm.cols * fm.channels();
            const float *data = (const float*)fm.data;
            for (int i=0; i<elements; i++)
                if (data[i] != data[i])
                    qFatal("%s NaN check %d failed!", qPrintable(src.file.flat()), index);
        }
    }
};

int CheckTransform::count = 0;

BR_REGISTER(Transform, CheckTransform)

/*!
 * \ingroup transforms
 * \brief Sets the template's matrix data to the br::File::name.
 * \author Josh Klontz \cite jklontz
 */
class NameTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        dst.file = src.file;
        QByteArray name = src.file.baseName().toLocal8Bit();
        dst = Mat(1, name.size(), CV_8UC1);
        memcpy(dst.m().data, name.data(), name.size());
    }
};

BR_REGISTER(Transform, NameTransform)

/*!
 * \ingroup transforms
 * \brief A no-op transform.
 * \see DiscardTransform FirstTransform RestTransform RemoveTransform
 * \author Josh Klontz \cite jklontz
 */
class IdentityTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        dst = src;
    }
};

BR_REGISTER(Transform, IdentityTransform)

/*!
 * \ingroup transforms
 * \brief Removes all template's matrices.
 * \see IdentityTransform FirstTransform RestTransform RemoveTransform
 * \author Josh Klontz \cite jklontz
 */
class DiscardTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        dst.file = src.file;
    }
};

BR_REGISTER(Transform, DiscardTransform)

/*!
 * \ingroup transforms
 * \brief Removes all but the first matrix from the template.
 * \see IdentityTransform DiscardTransform RestTransform RemoveTransform
 * \author Josh Klontz \cite jklontz
 */
class FirstTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        dst.file = src.file;
        dst = src.m();
    }
};

BR_REGISTER(Transform, FirstTransform)

/*!
 * \ingroup transforms
 * \brief Removes the first matrix from the template.
 * \see IdentityTransform DiscardTransform FirstTransform RemoveTransform
 * \author Josh Klontz \cite jklontz
 */
class RestTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        dst.removeFirst();
    }
};

BR_REGISTER(Transform, RestTransform)

/*!
 * \ingroup transforms
 * \brief Removes the matrix from the template at the specified index.
 * \author Josh Klontz \cite jklontz
 * \see IdentityTransform DiscardTransform FirstTransform RestTransform
 */
//! [example_transform]
class RemoveTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(int index READ get_index WRITE set_index RESET reset_index STORED false)
    BR_PROPERTY(int, index, 0)

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        dst.removeAt(index);
    }
};

BR_REGISTER(Transform, RemoveTransform)
//! [example_transform]

/*!
 * \ingroup transforms
 * \brief Rename metadata key
 * \author Josh Klontz \cite jklontz
 */
class RenameTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString find READ get_find WRITE set_find RESET reset_find STORED false)
    Q_PROPERTY(QString replace READ get_replace WRITE set_replace RESET reset_replace STORED false)
    BR_PROPERTY(QString, find, "")
    BR_PROPERTY(QString, replace, "")

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        if (dst.file.localKeys().contains(find)) {
            dst.file.set(replace, dst.file.value(find));
            dst.file.remove(find);
        }
    }
};

BR_REGISTER(Transform, RenameTransform)

/*!
 * \ingroup transforms
 * \brief Rename first found metadata key
 * \author Josh Klontz \cite jklontz
 */
class RenameFirstTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QStringList find READ get_find WRITE set_find RESET reset_find STORED false)
    Q_PROPERTY(QString replace READ get_replace WRITE set_replace RESET reset_replace STORED false)
    BR_PROPERTY(QStringList, find, QStringList())
    BR_PROPERTY(QString, replace, "")

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        foreach (const QString &key, find)
            if (dst.file.localKeys().contains(key)) {
                dst.file.set(replace, dst.file.value(key));
                dst.file.remove(key);
                break;
            }
    }
};

BR_REGISTER(Transform, RenameFirstTransform)

/*!
 * \ingroup transforms
 * \brief Name a point
 * \author Scott Klum \cite sklum
 */
class LabelTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QList<int> points READ get_points WRITE set_points RESET reset_points STORED false)
    Q_PROPERTY(QStringList names READ get_names WRITE set_names RESET reset_names STORED false)
    BR_PROPERTY(QList<int>, points, QList<int>())
    BR_PROPERTY(QStringList, names, QStringList())

    void project(const Template &src, Template &dst) const
    {
        if (points.size() != names.size()) qFatal("Point/name size mismatch");

        dst = src;

        for (int i=0; i<points.size(); i++)
            dst.file.set(names[i], points[i]);
    }
};

BR_REGISTER(Transform, LabelTransform)

/*!
 * \ingroup transforms
 * \brief Remove a name from a point
 * \author Scott Klum \cite sklum
 */
class AnonymizeTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QStringList names READ get_names WRITE set_names RESET reset_names STORED false)
    BR_PROPERTY(QStringList, names, QStringList())

    void project(const Template &src, Template &dst) const
    {
        dst = src;

        foreach (const QString &name, names)
            if (src.file.contains(name)) dst.file.appendPoint(src.file.get<QPointF>(name));
    }
};

BR_REGISTER(Transform, AnonymizeTransform)

// TODO: Use a global Mutex to prevent concurrent reads from stdin
#if 0
/*!
 * \ingroup transforms
 * \brief Name a point
 * \author Scott Klum \cite sklum
 */
class ElicitMetadataTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    Q_PROPERTY(QStringList metadata READ get_metadata WRITE set_metadata RESET reset_metadata STORED false)
    BR_PROPERTY(QStringList, metadata, QStringList())

    void init()
    {
        Globals->setProperty("parallelism", "0"); // Can only work in single threaded mode
    }

    void project(const Template &src, Template &dst) const
    {
        dst = src;

        QTextStream stream(stdin);

        foreach (const QString &key, metadata) {
            qDebug() << "Specify a value for key: " << key;
            QString value = stream.readLine();
            if (value[0] == '(') {
                QStringList values = value.split(',');
                if (values.size() == 2) /* QPointF */ {
                    values[1].chop(1);
                    QPointF point(values[0].mid(1).toFloat(), values[1].toFloat());
                    if (key != "Points") dst.file.set(key, point);
                    else dst.file.appendPoint(point);
                }
                else /* QRectF */ {
                    values[3].chop(1);
                    QRectF rect(values[0].mid(1).toFloat(), values[1].toFloat(), values[2].toFloat(), values[3].toFloat());
                    if (key != "Rects") dst.file.set(key, rect);
                    else dst.file.appendRect(rect);
                }
            }
            else dst.file.set(key, value);
        }
    }
};

BR_REGISTER(Transform, ElicitMetadataTransform)
#endif

/*!
 * \ingroup transforms
 * \brief Change the br::Template::file extension
 * \author Josh Klontz \cite jklontz
 */
class AsTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString extension READ get_extension WRITE set_extension RESET reset_extension STORED false)
    BR_PROPERTY(QString, extension, "")

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        dst.file.name = dst.file.name.left(dst.file.name.lastIndexOf('.')+1) + extension;
    }
};

BR_REGISTER(Transform, AsTransform)

/*!
 * \ingroup transforms
 * \brief Change the template subject using a regular expresion matched to the file's base name.
 * \author Josh Klontz \cite jklontz
 */
class SubjectTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString regexp READ get_regexp WRITE set_regexp RESET reset_regexp STORED false)
    BR_PROPERTY(QString, regexp, "(.*)")

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        QRegularExpression re(regexp);
        QRegularExpressionMatch match = re.match(dst.file.baseName());
        if (!match.hasMatch())
            qFatal("Unable to match regular expression \"%s\" to base name \"%s\"!", qPrintable(regexp), qPrintable(dst.file.baseName()));
        dst.file.set("Subject", match.captured(match.lastCapturedIndex()));
    }
};

BR_REGISTER(Transform, SubjectTransform)

/*!
 * \ingroup transforms
 * \brief Remove templates with the specified file extension or metadata value.
 * \author Josh Klontz \cite jklontz
 */
class RemoveTemplatesTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString regexp READ get_regexp WRITE set_regexp RESET reset_regexp STORED false)
    Q_PROPERTY(QString key READ get_key WRITE set_key RESET reset_key STORED false)
    BR_PROPERTY(QString, regexp, "")
    BR_PROPERTY(QString, key, "")

    void project(const Template &src, Template &dst) const
    {
        const QRegularExpression re(regexp);
        const QRegularExpressionMatch match = re.match(key.isEmpty() ? src.file.suffix() : src.file.get<QString>(key));
        if (match.hasMatch()) dst = Template();
        else                  dst = src;
    }
};

BR_REGISTER(Transform, RemoveTemplatesTransform)

/*!
 * \ingroup transforms
 * \brief Remove template metadata with the specified key(s).
 * \author Josh Klontz \cite jklontz
 */
class RemoveMetadataTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString regexp READ get_regexp WRITE set_regexp RESET reset_regexp STORED false)
    BR_PROPERTY(QString, regexp, "")

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        const QRegularExpression re(regexp);
        foreach (const QString &key, dst.file.localKeys())
            if (re.match(key).hasMatch())
                dst.file.remove(key);
    }
};

BR_REGISTER(Transform, RemoveMetadataTransform)

/*!
 * \ingroup transforms
 * \brief Store the last matrix of the input template as a metadata key with input property name.
 * \author Charles Otto \cite caotto
 */
class SaveMatTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    Q_PROPERTY(QString propName READ get_propName WRITE set_propName RESET reset_propName STORED false)
    BR_PROPERTY(QString, propName, "")

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        dst.file.set(propName, QVariant::fromValue(dst.m()));
    }
};
BR_REGISTER(Transform, SaveMatTransform)

/*!
 * \ingroup transforms
 * \brief Set the last matrix of the input template to a matrix stored as metadata with input propName.
 *
 * Also removes the property from the templates metadata after restoring it.
 *
 * \author Charles Otto \cite caotto
 */
class RestoreMatTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString propName READ get_propName WRITE set_propName RESET reset_propName STORED false)
    BR_PROPERTY(QString, propName, "")

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        if (dst.file.contains(propName)) {
            dst.clear();
            dst.m() = dst.file.get<cv::Mat>(propName);
            dst.file.remove(propName);
        }
    }
};
BR_REGISTER(Transform, RestoreMatTransform)

/*!
 * \ingroup transforms
 * \brief Expand the width and height of a template's rects by input width and height factors.
 * \author Charles Otto \cite caotto
 */
class ExpandRectTransform : public UntrainableTransform
{
    Q_OBJECT
    Q_PROPERTY(float widthExpand READ get_widthExpand WRITE set_widthExpand RESET reset_widthExpand STORED false)
    Q_PROPERTY(float heightExpand READ get_heightExpand WRITE set_heightExpand RESET reset_heightExpand STORED false)
    BR_PROPERTY(float, widthExpand, .5)
    BR_PROPERTY(float, heightExpand, .5)
    void project(const Template &src, Template &dst) const
    {
        dst = src;
        QList<QRectF> rects = dst.file.rects();
        for (int i=0;i < rects.size(); i++) {
            QRectF rect = rects[i];

            qreal width = rect.width();
            qreal height = rect.height();
            float half_w_expansion = widthExpand / 2;
            float half_h_expansion = heightExpand / 2;

            qreal half_width = width * widthExpand;
            qreal quarter_width = width * half_w_expansion;
            qreal half_height = height * heightExpand;
            qreal quarter_height = height * half_h_expansion;

            rect.setX(std::max(qreal(0),(rect.x() - quarter_width)));
            rect.setY(std::max(qreal(0),(rect.y() - quarter_height)));

            qreal x2 = std::min(rect.width() + half_width + rect.x(), qreal(src.m().cols) - 1);
            qreal y2 = std::min(rect.height() + half_height + rect.y(), qreal(src.m().rows) - 1);

            rect.setWidth(x2 - rect.x());
            rect.setHeight(y2 - rect.y());

            rects[i] = rect;
        }
        dst.file.setRects(rects);
    }
};

BR_REGISTER(Transform, ExpandRectTransform)


class EventTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString eventName READ get_eventName WRITE set_eventName RESET reset_eventName STORED false)
    BR_PROPERTY(QString, eventName, "")

    TemplateEvent event;

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        event.pulseSignal(dst);
    }

    TemplateEvent * getEvent(const QString & name)
    {
        return name == eventName ? &event : NULL;
    }
};
BR_REGISTER(Transform, EventTransform)

}

#include "misc.moc"
