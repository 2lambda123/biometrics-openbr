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
#include <numeric>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/imgproc/imgproc_c.h>
#include "openbr_internal.h"

#include "openbr/core/distance_sse.h"
#include "openbr/core/qtutils.h"
#include "openbr/core/opencvutils.h"

using namespace cv;

namespace br
{

/*!
 * \ingroup distances
 * \brief Standard distance metrics
 * \author Josh Klontz \cite jklontz
 */
class DistDistance : public Distance
{
    Q_OBJECT
    Q_ENUMS(Metric)
    Q_PROPERTY(Metric metric READ get_metric WRITE set_metric RESET reset_metric STORED false)
    Q_PROPERTY(bool negLogPlusOne READ get_negLogPlusOne WRITE set_negLogPlusOne RESET reset_negLogPlusOne STORED false)

public:
    /*!< */
    enum Metric { Correlation,
                  ChiSquared,
                  Intersection,
                  Bhattacharyya,
                  INF,
                  L1,
                  L2,
                  Cosine,
                  Dot};

private:
    BR_PROPERTY(Metric, metric, L2)
    BR_PROPERTY(bool, negLogPlusOne, true)

    float compare(const Mat &a, const Mat &b) const
    {
        if ((a.size != b.size) ||
            (a.type() != b.type()))
                return -std::numeric_limits<float>::max();

// TODO: this max value is never returned based on the switch / default 
        float result = std::numeric_limits<float>::max();
        switch (metric) {
          case Correlation:
            return compareHist(a, b, CV_COMP_CORREL);
          case ChiSquared:
            result = compareHist(a, b, CV_COMP_CHISQR);
            break;
          case Intersection:
            result = compareHist(a, b, CV_COMP_INTERSECT);
            break;
          case Bhattacharyya:
            result = compareHist(a, b, CV_COMP_BHATTACHARYYA);
            break;
          case INF:
            result = norm(a, b, NORM_INF);
            break;
          case L1:
            result = norm(a, b, NORM_L1);
            break;
          case L2:
            result = norm(a, b, NORM_L2);
            break;
          case Cosine:
            return cosine(a, b);
          case Dot:
            return a.dot(b);
          default:
            qFatal("Invalid metric");
        }

        if (result != result)
            qFatal("NaN result.");

        return negLogPlusOne ? -log(result+1) : result;
    }

    static float cosine(const Mat &a, const Mat &b)
    {
        float dot = 0;
        float magA = 0;
        float magB = 0;

        for (int row=0; row<a.rows; row++) {
            for (int col=0; col<a.cols; col++) {
                const float target = a.at<float>(row,col);
                const float query = b.at<float>(row,col);
                dot += target * query;
                magA += target * target;
                magB += query * query;
            }
        }

        return dot / (sqrt(magA)*sqrt(magB));
    }
};

BR_REGISTER(Distance, DistDistance)

/*!
 * \ingroup distances
 * \brief DistDistance wrapper.
 * \author Josh Klontz \cite jklontz
 */
class DefaultDistance : public Distance
{
    Q_OBJECT
    Distance *distance;

    void init()
    {
        distance = Distance::make("Dist("+file.suffix()+")");
    }

    float compare(const cv::Mat &a, const cv::Mat &b) const
    {
        return distance->compare(a, b);
    }
};

BR_REGISTER(Distance, DefaultDistance)

/*!
 * \ingroup distances
 * \brief Distances in series.
 * \author Josh Klontz \cite jklontz
 *
 * The templates are compared using each br::Distance in order.
 * If the result of the comparison with any given distance is -FLOAT_MAX then this result is returned early.
 * Otherwise the returned result is the value of comparing the templates using the last br::Distance.
 */
class PipeDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(QList<br::Distance*> distances READ get_distances WRITE set_distances RESET reset_distances)
    BR_PROPERTY(QList<br::Distance*>, distances, QList<br::Distance*>())

    void train(const TemplateList &data)
    {
        QFutureSynchronizer<void> futures;
        foreach (br::Distance *distance, distances)
            futures.addFuture(QtConcurrent::run(distance, &Distance::train, data));
        futures.waitForFinished();
    }

    float compare(const Template &a, const Template &b) const
    {
        float result = -std::numeric_limits<float>::max();
        foreach (br::Distance *distance, distances) {
            result = distance->compare(a, b);
            if (result == -std::numeric_limits<float>::max())
                return result;
        }
        return result;
    }
};

BR_REGISTER(Distance, PipeDistance)

/*!
 * \ingroup distances
 * \brief Fuses similarity scores across multiple matrices of compared templates
 * \author Scott Klum \cite sklum
 * \note Operation: Mean, sum, min, max are supported.
 */
class FuseDistance : public Distance
{
    Q_OBJECT
    Q_ENUMS(Operation)
    Q_PROPERTY(QStringList descriptions READ get_descriptions WRITE set_descriptions RESET reset_descriptions STORED false)
    Q_PROPERTY(Operation operation READ get_operation WRITE set_operation RESET reset_operation STORED false)
    Q_PROPERTY(QList<float> weights READ get_weights WRITE set_weights RESET reset_weights STORED false)
    Q_PROPERTY(QList<int> indices READ get_indices WRITE set_indices RESET reset_indices STORED false)

    QList<br::Distance*> distances;

public:
    /*!< */
    enum Operation {Mean, Sum, Max, Min};

private:
    BR_PROPERTY(QStringList, descriptions, QStringList())
    BR_PROPERTY(Operation, operation, Mean)
    BR_PROPERTY(QList<float>, weights, QList<float>())
    BR_PROPERTY(QList<int>, indices, QList<int>())

    void train(const TemplateList &src)
    {
        // Partition the templates by matrix
        QList<int> split;
        for (int i=0; i<src.at(0).size(); i++) split.append(1);

        QList<TemplateList> partitionedSrc = src.partition(split);

        if (!indices.isEmpty())
            for (int i=partitionedSrc.size()-1; i>=0; i--)
                if (!indices.contains(i)) partitionedSrc.removeAt(i);

        if (descriptions.size() != partitionedSrc.size()) qFatal("Incorrect number of distances supplied.");

        for (int i=0; i<descriptions.size(); i++)
            distances.append(make(descriptions[i]));

        // Train on each of the partitions
        for (int i=0; i<distances.size(); i++)
            distances[i]->train(partitionedSrc[i]);
    }

    float compare(const Template &a, const Template &b) const
    {
        if (a.size() != b.size()) qFatal("Comparison size mismatch");

        QList<float> scores;
        for (int i=0; i<distances.size(); i++) {
            int index = indices.isEmpty() ? i : indices[i];
            float weight;
            weights.isEmpty() ? weight = 1. : weight = weights[i];
            scores.append(weight*distances[i]->compare(Template(a.file, a[index]),Template(b.file, b[index])));
        }

        switch (operation) {
          case Mean:
            return std::accumulate(scores.begin(),scores.end(),0.0)/(float)scores.size();
            break;
          case Sum:
            return std::accumulate(scores.begin(),scores.end(),0.0);
            break;
          case Min:
            return *std::min_element(scores.begin(),scores.end());
            break;
          case Max:
            return *std::max_element(scores.begin(),scores.end());
            break;
          default:
            qFatal("Invalid operation.");
        }
    }

    void store(QDataStream &stream) const
    {
        foreach (Distance *distance, distances)
            distance->store(stream);
    }

    void load(QDataStream &stream)
    {
        for (int i=0; i<descriptions.size(); i++)
            distances.append(make(descriptions[i]));
        foreach (Distance *distance, distances)
            distance->load(stream);
    }
};

BR_REGISTER(Distance, FuseDistance)

/*!
 * \ingroup distances
 * \brief Fast 8-bit L1 distance
 * \author Josh Klontz \cite jklontz
 */
class ByteL1Distance : public Distance
{
    Q_OBJECT

    float compare(const Mat &a, const Mat &b) const
    {
        return l1(a.data, b.data, a.total());
    }
};

BR_REGISTER(Distance, ByteL1Distance)

/*!
 * \ingroup distances
 * \brief Fast 4-bit L1 distance
 * \author Josh Klontz \cite jklontz
 */
class HalfByteL1Distance : public Distance
{
    Q_OBJECT

    float compare(const Mat &a, const Mat &b) const
    {
        return packed_l1(a.data, b.data, a.total());
    }
};

BR_REGISTER(Distance, HalfByteL1Distance)

/*!
 * \ingroup distances
 * \brief Returns -log(distance(a,b)+1)
 * \author Josh Klontz \cite jklontz
 */
class NegativeLogPlusOneDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(br::Distance* distance READ get_distance WRITE set_distance RESET reset_distance STORED false)
    BR_PROPERTY(br::Distance*, distance, NULL)

    void train(const TemplateList &src)
    {
        distance->train(src);
    }

    float compare(const Template &a, const Template &b) const
    {
        return -log(distance->compare(a,b)+1);
    }

    void store(QDataStream &stream) const
    {
        distance->store(stream);
    }

    void load(QDataStream &stream)
    {
        distance->load(stream);
    }
};

BR_REGISTER(Distance, NegativeLogPlusOneDistance)

/*!
 * \ingroup distances
 * \brief Returns \c true if the templates are identical, \c false otherwise.
 * \author Josh Klontz \cite jklontz
 */
class IdenticalDistance : public Distance
{
    Q_OBJECT

    float compare(const Mat &a, const Mat &b) const
    {
        const size_t size = a.total() * a.elemSize();
        if (size != b.total() * b.elemSize()) return 0;
        for (size_t i=0; i<size; i++)
            if (a.data[i] != b.data[i]) return 0;
        return 1;
    }
};        

BR_REGISTER(Distance, IdenticalDistance)

/*!
 * \ingroup distances
 * \brief Online distance metric to attenuate match scores across multiple frames
 * \author Brendan klare \cite bklare
 */
class OnlineDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(br::Distance* distance READ get_distance WRITE set_distance RESET reset_distance STORED false)
    Q_PROPERTY(float alpha READ get_alpha WRITE set_alpha RESET reset_alpha STORED false)
    BR_PROPERTY(br::Distance*, distance, NULL)
    BR_PROPERTY(float, alpha, 0.1f)

    mutable QHash<QString,float> scoreHash;
    mutable QMutex mutex;

    float compare(const Template &target, const Template &query) const
    {
        float currentScore = distance->compare(target, query);

        QMutexLocker mutexLocker(&mutex);
        return scoreHash[target.file.name] = (1.0- alpha) * scoreHash[target.file.name] + alpha * currentScore;
    }
};

BR_REGISTER(Distance, OnlineDistance)

/*!
 * \ingroup distances
 * \brief Sum match scores across multiple distances
 * \author Scott Klum \cite sklum
 */
class SumDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(QList<br::Distance*> distances READ get_distances WRITE set_distances RESET reset_distances)
    BR_PROPERTY(QList<br::Distance*>, distances, QList<br::Distance*>())

    void train(const TemplateList &data)
    {
        QFutureSynchronizer<void> futures;
        foreach (br::Distance *distance, distances)
            futures.addFuture(QtConcurrent::run(distance, &Distance::train, data));
        futures.waitForFinished();
    }

    float compare(const Template &target, const Template &query) const
    {
        float result = 0;

        foreach (br::Distance *distance, distances) {
            float score = distance->compare(target, query);

            if (score == -std::numeric_limits<float>::max())
                return result;

            result += score;
        }

        return result;
    }
};

BR_REGISTER(Distance, SumDistance)

/*!
 * \ingroup transforms
 * \brief Compare each template to a fixed gallery (with name = galleryName), using the specified distance.
 * dst will contain a 1 by n vector of scores.
 * \author Charles Otto \cite caotto
 */
class GalleryCompareTransform : public Transform
{
    Q_OBJECT
    Q_PROPERTY(QString distanceAlgorithm READ get_distanceAlgorithm WRITE set_distanceAlgorithm RESET reset_distanceAlgorithm STORED false)
    Q_PROPERTY(QString galleryName READ get_galleryName WRITE set_galleryName RESET reset_galleryName STORED false)
    BR_PROPERTY(QString, distanceAlgorithm, "")
    BR_PROPERTY(QString, galleryName, "")

    TemplateList gallery;
    QSharedPointer<Distance> distance;

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        if (gallery.isEmpty())
            return;

        QList<float> line = distance->compare(gallery, src);
        dst.m() = OpenCVUtils::toMat(line, 1);
    }

    void init()
    {
        if (!galleryName.isEmpty()) {
            gallery = TemplateList::fromGallery(galleryName);
        }
        if (!distanceAlgorithm.isEmpty())
        {
            distance = Distance::fromAlgorithm(distanceAlgorithm);
        }
    }
};

BR_REGISTER(Transform, GalleryCompareTransform)


} // namespace br
#include "distance.moc"
