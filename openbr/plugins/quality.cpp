#include <QFutureSynchronizer>
#include <QtConcurrent>
#include "openbr_internal.h"

#include "openbr/core/common.h"
#include "openbr/core/opencvutils.h"

namespace br
{

/*!
 * \ingroup transforms
 * \brief Impostor Uniqueness Measure \cite klare12
 * \author Josh Klontz \cite jklontz
 */
class ImpostorUniquenessMeasureTransform : public Transform
{
    Q_OBJECT
    Q_PROPERTY(br::Distance* distance READ get_distance WRITE set_distance RESET reset_distance STORED false)
    Q_PROPERTY(double mean READ get_mean WRITE set_mean RESET reset_mean)
    Q_PROPERTY(double stddev READ get_stddev WRITE set_stddev RESET reset_stddev)
    BR_PROPERTY(br::Distance*, distance, Distance::make("Dist(L2)", this))
    BR_PROPERTY(double, mean, 0)
    BR_PROPERTY(double, stddev, 1)
    TemplateList impostors;

    float calculateIUM(const Template &probe, const TemplateList &gallery) const
    {
        const QString probeLabel = probe.file.get<QString>("Subject");
        TemplateList subset = gallery;
        for (int j=subset.size()-1; j>=0; j--)
            if (subset[j].file.get<QString>("Subject") == probeLabel)
                subset.removeAt(j);

        QList<float> scores = distance->compare(subset, probe);
        float min, max;
        Common::MinMax(scores, &min, &max);
        double mean = Common::Mean(scores);
        return (max-mean)/(max-min);
    }

    void train(const TemplateList &data)
    {
        distance->train(data);
        impostors = data;

        QList<float> iums; iums.reserve(impostors.size());
        for (int i=0; i<data.size(); i++)
            iums.append(calculateIUM(impostors[i], impostors));

        Common::MeanStdDev(iums, &mean, &stddev);
    }

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        float ium = calculateIUM(src, impostors);
        dst.file.set("Impostor_Uniqueness_Measure", ium);
        dst.file.set("Impostor_Uniqueness_Measure_Bin", ium < mean-stddev ? 0 : (ium < mean+stddev ? 1 : 2));
    }

    void store(QDataStream &stream) const
    {
        distance->store(stream);
        stream << mean << stddev << impostors;
    }

    void load(QDataStream &stream)
    {
        distance->load(stream);
        stream >> mean >> stddev >> impostors;
    }
};

BR_REGISTER(Transform, ImpostorUniquenessMeasureTransform)

/* Kernel Density Estimator */
struct KDE
{
    float min, max;
    double mean, stddev;
    QList<float> bins;

    KDE() : min(0), max(1), mean(0), stddev(1) {}
    KDE(const QList<float> &scores)
    {
        Common::MinMax(scores, &min, &max);
        Common::MeanStdDev(scores, &mean, &stddev);
        double h = Common::KernelDensityBandwidth(scores);
        const int size = 255;
        bins.reserve(size);
        for (int i=0; i<size; i++)
            bins.append(Common::KernelDensityEstimation(scores, min + (max-min)*i/(size-1), h));
    }

    float operator()(float score, bool gaussian = true) const
    {
        if (gaussian) return 1/(stddev*sqrt(2*CV_PI))*exp(-0.5*pow((score-mean)/stddev, 2));
        if (score <= min) return bins.first();
        if (score >= max) return bins.last();
        const float x = (score-min)/(max-min)*bins.size();
        const float y1 = bins[floor(x)];
        const float y2 = bins[ceil(x)];
        return y1 + (y2-y1)*(x-floor(x));
    }
};

QDataStream &operator<<(QDataStream &stream, const KDE &kde)
{
    return stream << kde.min << kde.max << kde.mean << kde.stddev << kde.bins;
}

QDataStream &operator>>(QDataStream &stream, KDE &kde)
{
    return stream >> kde.min >> kde.max >> kde.mean >> kde.stddev >> kde.bins;
}

/* Match Probability */
struct MP
{
    KDE genuine, impostor;
    MP() {}
    MP(const QList<float> &genuineScores, const QList<float> &impostorScores)
        : genuine(genuineScores), impostor(impostorScores) {}
    float operator()(float score, bool gaussian = true) const
    {
        const float g = genuine(score, gaussian);
        const float s = g / (impostor(score, gaussian) + g);
        return s;
    }
};

QDataStream &operator<<(QDataStream &stream, const MP &nmp)
{
    return stream << nmp.genuine << nmp.impostor;
}

QDataStream &operator>>(QDataStream &stream, MP &nmp)
{
    return stream >> nmp.genuine >> nmp.impostor;
}

/*!
 * \ingroup distances
 * \brief Match Probability \cite klare12
 * \author Josh Klontz \cite jklontz
 */
class MatchProbabilityDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(br::Distance* distance READ get_distance WRITE set_distance RESET reset_distance STORED false)
    Q_PROPERTY(bool gaussian READ get_gaussian WRITE set_gaussian RESET reset_gaussian STORED false)
    Q_PROPERTY(bool crossModality READ get_crossModality WRITE set_crossModality RESET reset_crossModality STORED false)

    MP mp;

    void train(const TemplateList &src)
    {
        distance->train(src);

        const QList<int> labels = src.indexProperty("Subject");
        QScopedPointer<MatrixOutput> matrixOutput(MatrixOutput::make(FileList(src.size()), FileList(src.size())));
        distance->compare(src, src, matrixOutput.data());

        QList<float> genuineScores, impostorScores;
        genuineScores.reserve(labels.size());
        impostorScores.reserve(labels.size()*labels.size());
        for (int i=0; i<src.size(); i++) {
            for (int j=0; j<i; j++) {
                const float score = matrixOutput.data()->data.at<float>(i, j);
                if (score == -std::numeric_limits<float>::max()) continue;
                if (crossModality) if(src[i].file.get<QString>("Modality") == src[j].file.get<QString>("Modality")) continue;
                if (labels[i] == labels[j]) genuineScores.append(score);
                else                        impostorScores.append(score);
            }
        }

        mp = MP(genuineScores, impostorScores);
    }

    float compare(const Template &target, const Template &query) const
    {
        float rawScore = distance->compare(target, query);
        if (rawScore == -std::numeric_limits<float>::max()) return rawScore;
        return mp(rawScore, gaussian);
    }

    void store(QDataStream &stream) const
    {
        distance->store(stream);
        stream << mp;
    }

    void load(QDataStream &stream)
    {
        distance->load(stream);
        stream >> mp;
    }

protected:
    BR_PROPERTY(br::Distance*, distance, make("Dist(L2)"))
    BR_PROPERTY(bool, gaussian, true)
    BR_PROPERTY(bool, crossModality, false)
};

BR_REGISTER(Distance, MatchProbabilityDistance)

/*!
 * \ingroup distances
 * \brief Match Probability modification for heat maps \cite klare12
 * \author Scott Klum \cite sklum
 */
class HeatMapDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(br::Distance* distance READ get_distance WRITE set_distance RESET reset_distance STORED false)
    Q_PROPERTY(bool gaussian READ get_gaussian WRITE set_gaussian RESET reset_gaussian STORED false)
    Q_PROPERTY(bool crossModality READ get_crossModality WRITE set_crossModality RESET reset_crossModality STORED false)
    Q_PROPERTY(int step READ get_step WRITE set_step RESET reset_step STORED false)
    BR_PROPERTY(br::Distance*, distance, make("Dist(L2)"))
    BR_PROPERTY(bool, gaussian, true)
    BR_PROPERTY(bool, crossModality, false)
    BR_PROPERTY(int, step, 1)

    QList<MP> mp;

    void train(const TemplateList &src)
    {
        distance->train(src);

        const QList<int> labels = src.indexProperty("Subject");

        QList<TemplateList> patches;

        // Split src into list of TemplateLists of corresponding patches across all Templates
        for (int i=0; i<step; i++) {
            TemplateList patchBuffer;
            for (int j=i; j<src.size(); j+=step) {
                patchBuffer.append(src[j]);
            }
            patches.append(patchBuffer);
            patchBuffer.clear();
        }

        QScopedPointer<MatrixOutput> matrixOutput(MatrixOutput::make(FileList(patches[0].size()), FileList(patches[0].size())));

        for (int i=0; i<step; i++) {
            distance->compare(patches[i], patches[i], matrixOutput.data());
            QList<float> genuineScores, impostorScores;
            genuineScores.reserve(step);
            impostorScores.reserve(step);
            for (int j=0; j<matrixOutput.data()->data.rows; j++) {
                for (int k=0; k<j; k++) {
                    const float score = matrixOutput.data()->data.at<float>(j, k);
                    if (score == -std::numeric_limits<float>::max()) continue;
                    if (crossModality) if(src[j*step].file.get<QString>("MODALITY") == src[k*step].file.get<QString>("MODALITY")) continue;
                    if (labels[j*step] == labels[k*step]) genuineScores.append(score);
                    else                                                        impostorScores.append(score);
                }
            }

            mp.append(MP(genuineScores, impostorScores));
        }
    }

    float compare(const Template &target, const Template &query) const
    {
        (void) target;
        (void) query;
        qFatal("You did this wrong");

        return 0;
    }

    // Switch this to template list version, use compare(template, template) in
    // heat map distance, and index into the proper match probability
    void compare(const TemplateList &target, const TemplateList &query, Output *output) const
    {
        for (int i=0; i<step; i++) {
            float rawScore = distance->compare(target[i],query[i]);
            if (rawScore == -std::numeric_limits<float>::max()) output->setRelative(rawScore, i, 0);
            else output->setRelative(mp[i](rawScore, gaussian), i, 0);
        }
     }

    void store(QDataStream &stream) const
    {
        distance->store(stream);
        stream << mp;
    }

    void load(QDataStream &stream)
    {
        distance->load(stream);
        stream >> mp;
    }
};

BR_REGISTER(Distance, HeatMapDistance)

/*!
 * \ingroup distances
 * \brief Linear normalizes of a distance so the mean impostor score is 0 and the mean genuine score is 1.
 * \author Josh Klontz \cite jklontz
 */
class UnitDistance : public Distance
{
    Q_OBJECT
    Q_PROPERTY(br::Distance *distance READ get_distance WRITE set_distance RESET reset_distance)
    Q_PROPERTY(float a READ get_a WRITE set_a RESET reset_a)
    Q_PROPERTY(float b READ get_b WRITE set_b RESET reset_b)
    BR_PROPERTY(br::Distance*, distance, make("Dist(L2)"))
    BR_PROPERTY(float, a, 1)
    BR_PROPERTY(float, b, 0)

    void train(const TemplateList &templates)
    {
        const TemplateList samples = templates.mid(0, 2000);
        const QList<int> sampleLabels = samples.indexProperty("Subject");
        QScopedPointer<MatrixOutput> matrixOutput(MatrixOutput::make(FileList(samples.size()), FileList(samples.size())));
        Distance::compare(samples, samples, matrixOutput.data());

        double genuineAccumulator, impostorAccumulator;
        int genuineCount, impostorCount;
        genuineAccumulator = impostorAccumulator = genuineCount = impostorCount = 0;

        for (int i=0; i<samples.size(); i++) {
            for (int j=0; j<i; j++) {
                const float val = matrixOutput.data()->data.at<float>(i, j);
                if (sampleLabels[i] == sampleLabels[j]) {
                    genuineAccumulator += val;
                    genuineCount++;
                } else {
                    impostorAccumulator += val;
                    impostorCount++;
                }
            }
        }

        if (genuineCount == 0) { qWarning("No genuine matches."); return; }
        if (impostorCount == 0) { qWarning("No impostor matches."); return; }

        double genuineMean = genuineAccumulator / genuineCount;
        double impostorMean = impostorAccumulator / impostorCount;

        if (genuineMean == impostorMean) { qWarning("Genuines and impostors are indistinguishable."); return; }

        a = 1.0/(genuineMean-impostorMean);
        b = impostorMean;

        qDebug("a = %f, b = %f", a, b);
    }

    float compare(const Template &target, const Template &query) const
    {
        return a * (distance->compare(target, query) - b);
    }
};

BR_REGISTER(Distance, UnitDistance)

} // namespace br

#include "quality.moc"
