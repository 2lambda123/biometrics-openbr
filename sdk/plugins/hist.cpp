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

#include <opencv2/imgproc/imgproc.hpp>
#include <openbr_plugin.h>

#include "core/common.h"
#include "core/opencvutils.h"

using namespace cv;

namespace br
{

/*!
 * \ingroup transforms
 * \brief Histograms the matrix
 * \author Josh Klontz \cite jklontz
 */
class HistTransform : public UntrainableTransform
{
    Q_OBJECT
    Q_PROPERTY(float max READ get_max WRITE set_max RESET reset_max STORED false)
    Q_PROPERTY(float min READ get_min WRITE set_min RESET reset_min STORED false)
    Q_PROPERTY(int dims READ get_dims WRITE set_dims RESET reset_dims STORED false)
    BR_PROPERTY(float, max, 256)
    BR_PROPERTY(float, min, 0)
    BR_PROPERTY(int, dims, -1)

    void project(const Template &src, Template &dst) const
    {
        const int dims = this->dims == -1 ? max - min : this->dims;

        std::vector<Mat> mv;
        split(src, mv);
        Mat m(mv.size(), dims, CV_32FC1);

        for (size_t i=0; i<mv.size(); i++) {
            int channels[] = {0};
            int histSize[] = {dims};
            float range[] = {min, max};
            const float* ranges[] = {range};
            Mat hist;
            calcHist(&mv[i], 1, channels, Mat(), hist, 1, histSize, ranges);
            memcpy(m.ptr(i), hist.ptr(), dims * sizeof(float));
        }

        dst += m;
    }
};

BR_REGISTER(Transform, HistTransform)

/*!
 * \ingroup transforms
 * \brief Converts each element to its rank-ordered value.
 * \author Josh Klontz \cite jklontz
 */
class RankTransform : public UntrainableTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        const Mat &m = src;
        assert(m.channels() == 1);
        dst = Mat(m.rows, m.cols, CV_32FC1);
        typedef QPair<float,int> Tuple;
        QList<Tuple> tuples = Common::Sort(OpenCVUtils::matrixToVector(m));

        float prevValue = 0;
        int prevRank = 0;
        for (int i=0; i<tuples.size(); i++) {
            int rank;
            if (tuples[i].first == prevValue) rank = prevRank;
            else                              rank = i;
            dst.m().at<float>(tuples[i].second / m.cols, tuples[i].second % m.cols) = rank;
            prevValue = tuples[i].first;
            prevRank = rank;
        }
    }
};

BR_REGISTER(Transform, RankTransform)

/*!
 * \ingroup transforms
 * \brief An integral histogram
 * \author Josh Klontz \cite jklontz
 */
class IntegralHistTransform : public UntrainableTransform
{
    Q_OBJECT
    Q_PROPERTY(int bins READ get_bins WRITE set_bins RESET reset_bins STORED false)
    Q_PROPERTY(int radius READ get_radius WRITE set_radius RESET reset_radius STORED false)
    BR_PROPERTY(int, bins, 256)
    BR_PROPERTY(int, radius, 16)

    void project(const Template &src, Template &dst) const
    {
        const Mat &m = src.m();
        if (m.type() != CV_8UC1) qFatal("IntegralHist requires 8UC1 matrices.");

        Mat integral(m.rows/radius+1, (m.cols/radius+1)*bins, CV_32SC1);
        integral.setTo(0);
        for (int i=1; i<integral.rows; i++) {
            for (int j=1; j<integral.cols; j+=bins) {
                for (int k=0; k<bins; k++) integral.at<qint32>(i, j+k) += integral.at<qint32>(i-1, j     +k);
                for (int k=0; k<bins; k++) integral.at<qint32>(i, j+k) += integral.at<qint32>(i  , j-bins+k);
                for (int k=0; k<bins; k++) integral.at<qint32>(i, j+k) -= integral.at<qint32>(i-1, j-bins+k);
                for (int k=0; k<radius; k++)
                    for (int l=0; l<radius; l++)
                        integral.at<qint32>(i, j+m.at<quint8>((i-1)*radius+k,(j/bins-1)*radius+l))++;
            }
        }
        dst = integral;
    }
};

BR_REGISTER(Transform, IntegralHistTransform)

/*!
 * \ingroup transforms
 * \brief Detects regions of low variance
 * \author Josh Klontz \cite jklontz
 */
class VarianceChangeDetectorTransform : public UntrainableTransform
{
    Q_OBJECT
    Q_PROPERTY(int bins READ get_bins WRITE set_bins RESET reset_bins STORED false)
    Q_PROPERTY(int radius READ get_radius WRITE set_radius RESET reset_radius STORED false)
    BR_PROPERTY(int, bins, 256)
    BR_PROPERTY(int, radius, 16)

    float stddev(const Mat &integral, int i, int j, int scale, int *buffer) const
    {
        const float count = scale*scale*radius*radius;

        float mean = 0;
        for (int k=0; k<bins; k++) {
            buffer[k] = integral.at<qint32>(i+scale,(j+scale)*bins+k)
                      - integral.at<qint32>(i+scale, j       *bins+k)
                      - integral.at<qint32>(i      ,(j+scale)*bins+k)
                      + integral.at<qint32>(i      , j       *bins+k);
            mean += k*buffer[k];
        }
        mean /= count;

        float variance = 0;
        for (int k=0; k<bins; k++)
            variance += buffer[k] * (k-mean) * (k-mean);

        return sqrt(variance/count);
    }

    void project(const Template &src, Template &dst) const
    {
        const Mat &m = src.m();
        if (m.type() != CV_32SC1) qFatal("VarianceChangeDetector requires CV_32SC1 images from IntegralHist");

        int *buffer = new int[bins];

        float bestRatio = -std::numeric_limits<float>::max();
        QRectF bestRect;

        const int rows = m.rows;
        const int cols = m.cols/bins;
        const int maxSize = min(m.rows, m.cols/bins);
        int scale = 2;
        while (scale < maxSize) {
            const int step = std::max(1, scale/6);
            for (int i=0; i+scale < rows; i+=step) {
                for (int j=0; j+scale < cols; j+=step) {
                    float internalStdDev = stddev(m, i, j, scale, buffer);
                    float externalStdDev = std::numeric_limits<float>::max();
                    externalStdDev = std::min(externalStdDev, ((i-2*scale >= 0)   && (j-2*scale >= 0))   ? stddev(m, i-2*scale, j-2*scale, scale, buffer) : 0);
                    externalStdDev = std::min(externalStdDev,  (i-2*scale >= 0)                          ? stddev(m, i-2*scale, j        , scale, buffer) : 0);
                    externalStdDev = std::min(externalStdDev, ((i-2*scale >= 0)   && (j+3*scale < cols)) ? stddev(m, i-2*scale, j+2*scale, scale, buffer) : 0);
                    externalStdDev = std::min(externalStdDev,                        (j+3*scale < cols)  ? stddev(m, i        , j+2*scale, scale, buffer) : 0);
                    externalStdDev = std::min(externalStdDev, ((i+3*scale < rows) && (j+3*scale < cols)) ? stddev(m, i+2*scale, j+2*scale, scale, buffer) : 0);
                    externalStdDev = std::min(externalStdDev,  (i+3*scale < rows)                        ? stddev(m, i+2*scale, j        , scale, buffer) : 0);
                    externalStdDev = std::min(externalStdDev, ((i+3*scale < rows) && (j-2*scale >= 0))   ? stddev(m, i+2*scale, j-2*scale, scale, buffer) : 0);
                    externalStdDev = std::min(externalStdDev,                        (j-2*scale >= 0)    ? stddev(m, i        , j-2*scale, scale, buffer) : 0);

                    float ratio;
                    if      (externalStdDev == 0) ratio = 0;
                    else if (internalStdDev == 0) ratio = std::numeric_limits<float>::max() * (float(scale)/float(maxSize));
                    else                          ratio = scale*scale * pow(externalStdDev,2) / pow(internalStdDev, 2);

                    if (ratio > bestRatio) {
                        bestRatio = ratio;
                        bestRect = QRect(j*radius, i*radius, scale*radius, scale*radius);
                    }
                }
            }
            scale = std::max(scale+1, int(scale*1.25));
        }

        delete[] buffer;
        dst.file.appendRect(bestRect);
        dst.file.setLabel(bestRatio);
    }
};

BR_REGISTER(Transform, VarianceChangeDetectorTransform)

} // namespace br

#include "hist.moc"
