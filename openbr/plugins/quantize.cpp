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

#include <QFutureSynchronizer>
#include <QtConcurrentRun>
#include <openbr/openbr_plugin.h>

#include "openbr/core/common.h"
#include "openbr/core/opencvutils.h"
#include "openbr/core/qtutils.h"

using namespace cv;

namespace br
{

/*!
 * \ingroup transforms
 * \brief Approximate floats as uchar.
 * \author Josh Klontz \cite jklontz
 */
class QuantizeTransform : public Transform
{
    Q_OBJECT
    Q_PROPERTY(float a READ get_a WRITE set_a RESET reset_a)
    Q_PROPERTY(float b READ get_b WRITE set_b RESET reset_b)
    BR_PROPERTY(float, a, 1)
    BR_PROPERTY(float, b, 0)

    void train(const TemplateList &data)
    {
        double minVal, maxVal;
        minMaxLoc(OpenCVUtils::toMat(data.data()), &minVal, &maxVal);
        a = 255.0/(maxVal-minVal);
        b = -a*minVal;
    }

    void project(const Template &src, Template &dst) const
    {
        src.m().convertTo(dst, CV_8U, a, b);
    }
};

BR_REGISTER(Transform, QuantizeTransform)

/*!
 * \ingroup distances
 * \brief Bayesian quantization distance
 * \author Josh Klontz \cite jklontz
 */
class BayesianQuantizationDistance : public Distance
{
    Q_OBJECT
    QVector<float> loglikelihood;

    void train(const TemplateList &src)
    {
        if (src.first().size() > 1)
            qFatal("Expected sigle matrix templates.");

        Mat data = OpenCVUtils::toMat(src.data());
        QList<int> labels = src.labels<int>();

        QVector<qint64> genuines(256*256,0), impostors(256*256,0);
        for (int i=0; i<labels.size(); i++) {
            const uchar *a = data.ptr(i);
            for (int j=0; j<labels.size(); j++) {
                const uchar *b = data.ptr(j);
                const bool genuine = (labels[i] == labels[j]);
                for (int k=0; k<data.cols; k++)
                    genuine ? genuines[256*a[k]+b[k]]++ : impostors[256*a[k]+b[k]]++;
            }
        }

        qint64 totalGenuines(0), totalImpostors(0);
        for (int i=0; i<256*256; i++) {
            totalGenuines += genuines[i];
            totalImpostors += impostors[i];
        }

        loglikelihood = QVector<float>(256*256);
        for (int i=0; i<256; i++)
            for (int j=0; j<256; j++)
                loglikelihood[i*256+j] = log((double(genuines[i*256+j]+genuines[j*256+i]+1)/totalGenuines)/
                                             (double(impostors[i*256+j]+impostors[j*256+i]+1)/totalImpostors));
    }

    float compare(const Template &a, const Template &b) const
    {
        const uchar *aData = a.m().data;
        const uchar *bData = b.m().data;
        const int size = a.m().rows * a.m().cols;
        float likelihood = 0;
        for (int i=0; i<size; i++)
            likelihood += loglikelihood[256*aData[i]+bData[i]];
        return likelihood;
    }

    void load(QDataStream &stream)
    {
        stream >> loglikelihood;
    }

    void store(QDataStream &stream) const
    {
        stream << loglikelihood;
    }
};

BR_REGISTER(Distance, BayesianQuantizationDistance)

/*!
 * \ingroup transforms
 * \brief Approximate floats as signed bit.
 * \author Josh Klontz \cite jklontz
 */
class BinarizeTransform : public UntrainableTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        const Mat &m = src;
        if ((m.cols % 8 != 0) || (m.type() != CV_32FC1))
            qFatal("Expected CV_32FC1 matrix with a multiple of 8 columns.");
        Mat n(m.rows, m.cols/8, CV_8UC1);
        for (int i=0; i<m.rows; i++)
            for (int j=0; j<m.cols-7; j+=8)
                n.at<uchar>(i,j) = ((m.at<float>(i,j+0) > 0) << 0) +
                                   ((m.at<float>(i,j+1) > 0) << 1) +
                                   ((m.at<float>(i,j+2) > 0) << 2) +
                                   ((m.at<float>(i,j+3) > 0) << 3) +
                                   ((m.at<float>(i,j+4) > 0) << 4) +
                                   ((m.at<float>(i,j+5) > 0) << 5) +
                                   ((m.at<float>(i,j+6) > 0) << 6) +
                                   ((m.at<float>(i,j+7) > 0) << 7);
        dst = n;
    }
};

BR_REGISTER(Transform, BinarizeTransform)

/*!
 * \ingroup transforms
 * \brief Compress two uchar into one uchar.
 * \author Josh Klontz \cite jklontz
 */
class PackTransform : public UntrainableTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        const Mat &m = src;
        if ((m.cols % 2 != 0) || (m.type() != CV_8UC1))
            qFatal("Invalid template format.");

        Mat n(m.rows, m.cols/2, CV_8UC1);
        for (int i=0; i<m.rows; i++)
            for (int j=0; j<m.cols/2; j++)
                n.at<uchar>(i,j) = ((m.at<uchar>(i,2*j+0) >> 4) << 4) +
                                   ((m.at<uchar>(i,2*j+1) >> 4) << 0);
        dst = n;
    }
};

BR_REGISTER(Transform, PackTransform)

QVector<Mat> ProductQuantizationLUTs;

/*!
 * \ingroup distances
 * \brief Distance in a product quantized space \cite jegou11
 * \author Josh Klontz
 */
class ProductQuantizationDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(bool bayesian READ get_bayesian WRITE set_bayesian RESET reset_bayesian STORED false)
    BR_PROPERTY(bool, bayesian, false)

    float compare(const Template &a, const Template &b) const
    {
        float distance = 0;
        for (int i=0; i<a.size(); i++) {
            const int elements = a[i].total();
            const uchar *aData = a[i].data;
            const uchar *bData = b[i].data;
            const float *lut = (const float*)ProductQuantizationLUTs[i].data;
            for (int j=0; j<elements; j++)
                 distance += lut[j*256*256 + aData[j]*256+bData[j]];
        }
        if (!bayesian) distance = -log(distance+1);
        return distance;
    }
};

BR_REGISTER(Distance, ProductQuantizationDistance)

/*!
 * \ingroup transforms
 * \brief Product quantization \cite jegou11
 * \author Josh Klontz \cite jklontz
 */
class ProductQuantizationTransform : public Transform
{
    Q_OBJECT
    Q_PROPERTY(int n READ get_n WRITE set_n RESET reset_n STORED false)
    Q_PROPERTY(br::Distance *distance READ get_distance WRITE set_distance RESET reset_distance STORED false)
    Q_PROPERTY(bool bayesian READ get_bayesian WRITE set_bayesian RESET reset_bayesian STORED false)
    BR_PROPERTY(int, n, 2)
    BR_PROPERTY(br::Distance*, distance, Distance::make("L2", this))
    BR_PROPERTY(bool, bayesian, false)

    int index;
    QList<Mat> centers;

public:
    ProductQuantizationTransform()
    {
        index = ProductQuantizationLUTs.size();
        ProductQuantizationLUTs.append(Mat());
    }

private:
    static void getScores(const QList<int> &indicies, const QList<int> &labels, const Mat &lut, QVector<float> &genuineScores, QVector<float> &impostorScores)
    {
        genuineScores.clear(); impostorScores.clear();
        genuineScores.reserve(indicies.size());
        impostorScores.reserve(indicies.size()*indicies.size()/2);
        for (int i=0; i<indicies.size(); i++)
            for (int j=i+1; j<indicies.size(); j++) {
                const float score = lut.at<float>(0, indicies[i]*256+indicies[j]);
                if (labels[i] == labels[j]) genuineScores.append(score);
                else                        impostorScores.append(score);
            }
    }

    void _train(const Mat &data, const QList<int> &labels, Mat *lut, Mat *center)
    {
        Mat clusterLabels;
        kmeans(data, 256, clusterLabels, TermCriteria(TermCriteria::MAX_ITER, 10, 0), 3, KMEANS_PP_CENTERS, *center);

        for (int j=0; j<256; j++)
            for (int k=0; k<256; k++)
                lut->at<float>(0,j*256+k) = distance->compare(center->row(j), center->row(k));

        if (!bayesian) return;

        QList<int> indicies = OpenCVUtils::matrixToVector<int>(clusterLabels);
        QVector<float> genuineScores, impostorScores;

        // RBF Kernel
//        getScores(indicies, labels, *lut, genuineScores, impostorScores);
//        float sigma = 1.0 / Common::Mean(impostorScores);
//        for (int j=0; j<256; j++)
//            for (int k=0; k<256; k++)
//                lut->at<float>(0,j*256+k) = exp(-lut->at<float>(0,j*256+k)/(2*pow(sigma, 2.f)));

        // Bayesian PDF
        getScores(indicies, labels, *lut, genuineScores, impostorScores);
        genuineScores = Common::Downsample(genuineScores, 256);
        impostorScores = Common::Downsample(impostorScores, 256);

        double hGenuine = Common::KernelDensityBandwidth(genuineScores);
        double hImpostor = Common::KernelDensityBandwidth(impostorScores);

        for (int j=0; j<256; j++)
            for (int k=0; k<256; k++)
                lut->at<float>(0,j*256+k) = log(Common::KernelDensityEstimation(genuineScores, lut->at<float>(0,j*256+k), hGenuine) /
                                                Common::KernelDensityEstimation(impostorScores, lut->at<float>(0,j*256+k), hImpostor));
//                lut->at<float>(0,j*256+k) = std::max(0.0, log(Common::KernelDensityEstimation(genuineScores, lut->at<float>(0,j*256+k), hGenuine) /
//                                                              Common::KernelDensityEstimation(impostorScores, lut->at<float>(0,j*256+k), hImpostor)));
    }

    int getStep(int cols) const
    {
        if (n > 0) return n;
        if (n == 0) return cols;
        return ceil(float(cols)/abs(n));
    }

    int getOffset(int cols) const
    {
        if (n >= 0) return 0;
        const int step = getStep(cols);
        return (step - cols%step) % step;
    }

    int getDims(int cols) const
    {
        const int step = getStep(cols);
        if (n >= 0) return cols/step;
        return ceil(float(cols)/step);
    }

    void train(const TemplateList &src)
    {
        Mat data = OpenCVUtils::toMat(src.data());
        const int step = getStep(data.cols);
        const QList<int> labels = src.labels<int>();

        Mat &lut = ProductQuantizationLUTs[index];
        lut = Mat(getDims(data.cols), 256*256, CV_32FC1);

        QList<Mat> subdata, subluts;
        const int offset = getOffset(data.cols);
        for (int i=0; i<lut.rows; i++) {
            centers.append(Mat());
            subdata.append(data.colRange(max(0, i*step-offset), (i+1)*step-offset));
            subluts.append(lut.row(i));
        }

        QFutureSynchronizer<void> futures;
        for (int i=0; i<lut.rows; i++) {
            if (Globals->parallelism) futures.addFuture(QtConcurrent::run(this, &ProductQuantizationTransform::_train, subdata[i], labels, &subluts[i], &centers[i]));
            else                                                                                               _train (subdata[i], labels, &subluts[i], &centers[i]);
        }
        QtUtils::releaseAndWait(futures);
    }

    int getIndex(const Mat &m, const Mat &center) const
    {
        int bestIndex = 0;
        double bestDistance = std::numeric_limits<double>::max();
        for (int j=0; j<256; j++) {
            double distance = norm(m, center.row(j), NORM_L2);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = j;
            }
        }
        return bestIndex;
    }

    void project(const Template &src, Template &dst) const
    {
        Mat m = src.m().reshape(1, 1);
        const int step = getStep(m.cols);
        const int offset = getOffset(m.cols);
        dst = Mat(1, getDims(m.cols), CV_8UC1);
        for (int i=0; i<dst.m().cols; i++)
            dst.m().at<uchar>(0,i) = getIndex(m.colRange(max(0, i*step-offset), (i+1)*step-offset), centers[i]);
    }

    void store(QDataStream &stream) const
    {
        stream << centers << ProductQuantizationLUTs[index];
    }

    void load(QDataStream &stream)
    {
        stream >> centers >> ProductQuantizationLUTs[index];
    }
};

BR_REGISTER(Transform, ProductQuantizationTransform)

} // namespace br

#include "quantize.moc"
