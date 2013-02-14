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

#include <QDate>
#ifndef BR_EMBEDDED
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtXml>
#endif // BR_EMBEDDED
#include <opencv2/highgui/highgui.hpp>
#include <openbr_plugin.h>

#include "core/bee.h"
#include "core/opencvutils.h"
#include "core/qtutils.h"

using namespace cv;

namespace br
{

 /*!
 * \ingroup formats
 * \brief Read all frames of a video using OpenCV 
 * \author Charles Otto \cite caotto
 */
class videoFormat : public Format
{
    Q_OBJECT

public:
    Template read() const
    {
        VideoCapture videoSource(file.name.toStdString());
        videoSource.open(file.name.toStdString() );
        
    
        Template frames;
        if (!videoSource.isOpened()) {
            qWarning("video file open failed");
            return frames;
        }
        int res = (int) videoSource.get(CV_CAP_PROP_FOURCC);

        bool open = true;
        while(open) {
            cv::Mat frame;
            open = videoSource.read(frame);
            if (!open) break;

            frames.append(cv::Mat());
            frames.back() = frame.clone();
        }

        return frames;
    }

    void write(const Template &t) const
    {
        int fourcc = OpenCVUtils::getFourcc(); 
        VideoWriter videoSink(file.name.toStdString(), fourcc, 30, t.begin()->size());

        // Did we successfully open the output file?
        if (!videoSink.isOpened() ) qFatal("Failed to open output file");

        for (Template::const_iterator it = t.begin(); it!= t.end(); ++it) {
            videoSink << *it;
        }
    }
};

BR_REGISTER(Format, videoFormat)

/*!
 * \ingroup formats
 * \brief A simple binary matrix format.
 * \author Josh Klonyz \cite jklontz
 * First 4 bytes indicate the number of rows.
 * Second 4 bytes indicate the number of columns.
 * The rest of the bytes are 32-bit floating data elements in row-major order.
 */
class binFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        QByteArray data;
        QtUtils::readFile(file, data);
        return Template(file, Mat(((quint32*)data.data())[0],
                                  ((quint32*)data.data())[1],
                                  CV_32FC1,
                                  data.data()+8).clone());
    }

    void write(const Template &t) const
    {
        Mat m;
        t.m().convertTo(m, CV_32F);
        if (m.channels() != 1) qFatal("binFormat::write only supports single channel matrices.");

        QByteArray data;
        QDataStream stream(&data, QFile::WriteOnly);
        stream.writeRawData((const char*)&m.rows, 4);
        stream.writeRawData((const char*)&m.cols, 4);
        stream.writeRawData((const char*)m.data, 4*m.rows*m.cols);

        QtUtils::writeFile(file, data);
    }
};

BR_REGISTER(Format, binFormat)

/*!
 * \ingroup formats
 * \brief Reads a comma separated value file.
 * \author Josh Klontz \cite jklontz
 */
class csvFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        QFile f(file.name);
        f.open(QFile::ReadOnly);
        QStringList lines(QString(f.readAll()).split('\n'));
        f.close();

        bool isUChar = true;
        QList< QList<float> > valsList;
        foreach (const QString &line, lines) {
            QList<float> vals;
            foreach (const QString &word, line.split(QRegExp(" *, *"), QString::SkipEmptyParts)) {
                bool ok;
                const float val = word.toFloat(&ok);
                vals.append(val);
                isUChar = isUChar && (val == float(uchar(val)));
            }
            if (!vals.isEmpty())
                valsList.append(vals);
        }

        Mat m(valsList.size(), valsList[0].size(), CV_32FC1);
        for (int i=0; i<valsList.size(); i++)
            for (int j=0; j<valsList[i].size(); j++)
                m.at<float>(i,j) = valsList[i][j];

        if (isUChar) m.convertTo(m, CV_8U);
        return Template(m);
    }

    void write(const Template &t) const
    {
        const Mat &m = t.m();
        if (t.size() != 1) qFatal("csvFormat::write only supports single matrix templates.");
        if (m.channels() != 1) qFatal("csvFormat::write only supports single channel matrices.");

        QStringList lines; lines.reserve(m.rows);
        for (int r=0; r<m.rows; r++) {
            QStringList elements; elements.reserve(m.cols);
            for (int c=0; c<m.cols; c++)
                elements.append(OpenCVUtils::elemToString(m, r, c));
            lines.append(elements.join(","));
        }

        QtUtils::writeFile(file, lines);
    }
};

BR_REGISTER(Format, csvFormat)

/*!
 * \ingroup formats
 * \brief Reads image files.
 * \author Josh Klontz \cite jklontz
 */
class DefaultFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        Template t;

        if (file.name.startsWith("http://") || file.name.startsWith("www.")) {
#ifndef BR_EMBEDDED
            QNetworkAccessManager networkAccessManager;
            QNetworkRequest request(file.name);
            request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
            QNetworkReply *reply = networkAccessManager.get(request);

            while (!reply->isFinished()) QCoreApplication::processEvents();
            if (reply->error()) qWarning("Url::read %s (%s).\n", qPrintable(reply->errorString()), qPrintable(QString::number(reply->error())));

            QByteArray data = reply->readAll();
            delete reply;

            Mat m = imdecode(Mat(1, data.size(), CV_8UC1, data.data()), 1);
            if (m.data) t.append(m);
#endif // BR_EMBEDDED
        } else {
            QString prefix = "";
            if (!QFileInfo(file.name).exists()) prefix = file.getString("path") + "/";
            Mat m = imread((prefix+file.name).toStdString());
            if (m.data) {
                t.append(m);
            }
            else {
                videoFormat videoReader;
                videoReader.file = file;
                t = videoReader.read();
            }
        }

        return t;
    }

    void write(const Template &t) const
    {
        if (t.size() != 1) {
            videoFormat videoWriter;
            videoWriter.file = file;
            videoWriter.write(t);
        }
        else {
            imwrite(file.name.toStdString(), t);
        }
    }
};

BR_REGISTER(Format, DefaultFormat)

/*!
 * \ingroup formats
 * \brief Reads a NIST BEE similarity matrix.
 * \author Josh Klontz \cite jklontz
 */
class mtxFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        return BEE::readSimmat(file);
    }

    void write(const Template &t) const
    {
        BEE::writeSimmat(t, file);
    }
};

BR_REGISTER(Format, mtxFormat)

/*!
 * \ingroup formats
 * \brief Reads a NIST BEE mask matrix.
 * \author Josh Klontz \cite jklontz
 */
class maskFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        return BEE::readMask(file);
    }

    void write(const Template &t) const
    {
        BEE::writeMask(t, file);
    }
};

BR_REGISTER(Format, maskFormat)

/*!
 * \ingroup formats
 * \brief MATLAB <tt>.mat</tt> format.
 * \author Josh Klontz \cite jklontz
 * http://www.mathworks.com/help/pdf_doc/matlab/matfile_format.pdf
 */
class matFormat : public Format
{
    Q_OBJECT

    struct Element
    {
        quint32 type, bytes;
        QByteArray data;
        Element() : type(0), bytes(0) {}
        Element(QDataStream &stream)
            : type(0), bytes(0)
        {
            bool error = false;
            error |= (stream.readRawData((char*)&type, 4) != 4);

            if (type >= 1 << 16) {
                // Small data format
                bytes = type;
                type = type & 0x0000FFFF;
                bytes = bytes >> 16;
            } else {
                // Regular format
                error |= (stream.readRawData((char*)&bytes, 4) != 4);
            }

            data.resize(bytes);
            error |= (int(bytes) != stream.readRawData(data.data(), bytes));

            // Alignment
            int skipBytes = (bytes < 4) ? (4 - bytes) : (8 - bytes%8)%8;
            if (skipBytes != 0) stream.skipRawData(skipBytes);

            if (error) qFatal("matFormat::Element Unexpected end of file.");
        }

        void print() const
        {
            qDebug() << "matFormat::Element" << type << bytes << data.size();
        }
    };

    Template read() const
    {
        QByteArray byteArray;
        QtUtils::readFile(file, byteArray);
        QDataStream f(byteArray);

        { // Check header
            QByteArray header(128, 0);
            f.readRawData(header.data(), 128);
            if (!header.startsWith("MATLAB 5.0 MAT-file"))
                qFatal("matFormat::read Invalid MAT header.");
        }

        Template t(file);

        while (!f.atEnd()) {
            Element element(f);

            // miCOMPRESS
            if (element.type == 15) {
                element.data.prepend((char*)&element.bytes, 4); // Qt zlib wrapper requires this to preallocate the buffer
                QDataStream uncompressed(qUncompress(element.data));
                element = Element(uncompressed);
            }

            // miMATRIX
            if (element.type == 14) {
                QDataStream matrix(element.data);
                qint32 rows = 0, columns = 0;
                int matrixType = 0;
                QByteArray matrixData;
                while (!matrix.atEnd()) {
                    Element subelement(matrix);
                    if (subelement.type == 5) { // Dimensions array
                        if (subelement.bytes == 8) {
                            rows = ((qint32*)subelement.data.data())[0];
                            columns = ((qint32*)subelement.data.data())[1];
                        } else {
                            qWarning("matFormat::read can only handle 2D arrays.");
                        }
                    } else if (subelement.type == 7) { //miSINGLE
                        matrixType = CV_32FC1;
                        matrixData = subelement.data;
                    } else if (subelement.type == 9) { //miDOUBLE
                        matrixType = CV_64FC1;
                        matrixData = subelement.data;
                    }
                }

                if ((rows > 0) && (columns > 0) && (matrixType != 0) && !matrixData.isEmpty()) {
                    Mat transposed;
                    transpose(Mat(rows, columns, matrixType, matrixData.data()), transposed);
                    t.append(transposed);
                }
            }
        }

        return t;
    }

    void write(const Template &t) const
    {
        QByteArray data;
        QDataStream stream(&data, QFile::WriteOnly);

        { // Header
            QByteArray header = "MATLAB 5.0 MAT-file; Made with OpenBR | www.openbiometrics.org\n";
            QByteArray buffer(116-header.size(), 0);
            stream.writeRawData(header.data(), header.size());
            stream.writeRawData(buffer.data(), buffer.size());
            quint64 subsystem = 0;
            quint16 version = 0x0100;
            const char *endianness = "IM";
            stream.writeRawData((const char*)&subsystem, 8);
            stream.writeRawData((const char*)&version, 2);
            stream.writeRawData(endianness, 2);
        }

        for (int i=0; i<t.size(); i++) {
            const Mat &m = t[i];
            if (m.channels() != 1) qFatal("matFormat::write only supports single channel matrices.");

            QByteArray subdata;
            QDataStream substream(&subdata, QFile::WriteOnly);

            {  // Array Flags
                quint32 type = 6;
                quint32 bytes = 8;
                quint64 arrayClass = 0;
                switch (m.type()) {
                  case CV_64FC1: arrayClass = 6; break;
                  case CV_32FC1: arrayClass = 7; break;
                  case CV_8UC1: arrayClass = 8; break;
                  case CV_8SC1: arrayClass = 9; break;
                  case CV_16UC1: arrayClass = 10; break;
                  case CV_16SC1: arrayClass = 11; break;
                  case CV_32SC1: arrayClass = 12; break;
                  default: qFatal("matFormat::write unsupported matrix class.");
                }
                substream.writeRawData((const char*)&type, 4);
                substream.writeRawData((const char*)&bytes, 4);
                substream.writeRawData((const char*)&arrayClass, 8);
            }

            { // Dimensions Array
                quint32 type = 5;
                quint32 bytes = 8;
                substream.writeRawData((const char*)&type, 4);
                substream.writeRawData((const char*)&bytes, 4);
                substream.writeRawData((const char*)&m.rows, 4);
                substream.writeRawData((const char*)&m.cols, 4);
            }

            { // Array Name
                QByteArray name(qPrintable(QString("OpenBR_%1").arg(QString::number(i))));
                quint32 type = 1;
                quint32 bytes = name.size();
                QByteArray buffer((8 - bytes%8)%8, 0);
                substream.writeRawData((const char*)&type, 4);
                substream.writeRawData((const char*)&bytes, 4);
                substream.writeRawData(name.data(), name.size());
                substream.writeRawData(buffer.data(), buffer.size());
            }

            { // Real part
                quint32 type = 0;
                switch (m.type()) {
                  case CV_8SC1:  type = 1; break;
                  case CV_8UC1:  type = 2; break;
                  case CV_16SC1: type = 3; break;
                  case CV_16UC1: type = 4; break;
                  case CV_32SC1: type = 5; break;
                  case CV_32FC1: type = 7; break;
                  case CV_64FC1: type = 9; break;
                  default: qFatal("matFormat::write unsupported matrix type.");
                }
                quint32 bytes = m.elemSize() * m.rows * m.cols;
                QByteArray buffer((8 - bytes%8)%8, 0);
                Mat transposed;
                transpose(m, transposed);
                substream.writeRawData((const char*)&type, 4);
                substream.writeRawData((const char*)&bytes, 4);
                substream.writeRawData((const char*)transposed.data, bytes);
                substream.writeRawData(buffer.data(), buffer.size());
            }

            { // Matrix
                quint32 type = 14;
                quint32 bytes = subdata.size();
                stream.writeRawData((const char*)&type, 4);
                stream.writeRawData((const char*)&bytes, 4);
                stream.writeRawData(subdata.data(), subdata.size());
            }
        }

        QtUtils::writeFile(file, data);
    }
};

BR_REGISTER(Format, matFormat)

/*!
 * \ingroup formats
 * \brief Retrieves an image from a webcam.
 * \author Josh Klontz \cite jklontz
 */
class webcamFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        static QScopedPointer<VideoCapture> videoCapture;

        if (videoCapture.isNull())
            videoCapture.reset(new VideoCapture(0));

        Mat m;
        videoCapture->read(m);
        return Template(m);
    }

    void write(const Template &t) const
    {
        (void) t;
        qFatal("webcamFormat::write not supported.");
    }
};

BR_REGISTER(Format, webcamFormat)

#ifndef BR_EMBEDDED
/*!
 * \ingroup formats
 * \brief Decodes images from Base64 xml
 * \author Scott Klum \cite sklum
 * \author Josh Klontz \cite jklontz
 */
class xmlFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        QDomDocument doc(file);
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) qFatal("xmlFormat::read unable to open %s for reading.", qPrintable(file.flat()));
        if (!doc.setContent(&f))          qFatal("xmlFormat::read unable to parse %s.", qPrintable(file.flat()));
        f.close();

        Template t;
        QDomElement docElem = doc.documentElement();
        QDomNode subject = docElem.firstChild();
        while (!subject.isNull()) {
            QDomNode fileNode = subject.firstChild();

            while (!fileNode.isNull()) {
                QDomElement e = fileNode.toElement();

                if (e.tagName() == "FORMAL_IMG") {
                    QByteArray byteArray = QByteArray::fromBase64(qPrintable(e.text()));
                    Mat m = imdecode(Mat(3, byteArray.size(), CV_8UC3, byteArray.data()), CV_LOAD_IMAGE_COLOR);
                    if (!m.data) qWarning("xmlFormat::read failed to decode image data.");
                    t.append(m);
                } else if ((e.tagName() == "RELEASE_IMG") ||
                           (e.tagName() == "PREBOOK_IMG") ||
                           (e.tagName() == "LPROFILE") ||
                           (e.tagName() == "RPROFILE")) {
                    // Ignore these other image fields for now
                } else {
                    t.file.insert(e.tagName(), e.text());
                }

                fileNode = fileNode.nextSibling();
            }
            subject = subject.nextSibling();
        }

        // Calculate age
        if (t.file.contains("DOB")) {
            const QDate dob = QDate::fromString(t.file.getString("DOB").left(10), "yyyy-MM-dd");
            const QDate current = QDate::currentDate();
            int age = current.year() - dob.year();
            if (current.month() < dob.month()) age--;
            t.file.insert("Age", age);
        }

        return t;
    }

    void write(const Template &t) const
    {
        (void) t;
        qFatal("xmlFormat::write not supported.");
    }
};

BR_REGISTER(Format, xmlFormat)
#endif // BR_EMBEDDED

} // namespace br

#include "format.moc"
