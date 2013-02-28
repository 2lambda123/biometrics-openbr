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

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QList>
#ifndef BR_EMBEDDED
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#endif // BR_EMBEDDED
#include <QMutex>
#include <QPair>
#include <QVector>
#include <QtGlobal>
#include <opencv2/highgui/highgui.hpp>
#include <iostream>
#include <limits>
#include <assert.h>
#include <openbr_plugin.h>

#include "core/bee.h"
#include "core/common.h"
#include "core/opencvutils.h"
#include "core/qtutils.h"

namespace br
{
/*!
 * \ingroup outputs
 * \brief Adaptor class -- write a matrix output using Format classes.
 * \author Charles Otto \cite caotto
 */
class DefaultOutput : public MatrixOutput
{
    Q_OBJECT

    ~DefaultOutput()
    {
        if (file.isNull() || targetFiles.isEmpty() || queryFiles.isEmpty()) return;

        br::Template T(this->file, this->data);
        QScopedPointer<Format> writer(Factory<Format>::make(this->file));
        writer->write(T);
    }
};

BR_REGISTER(Output, DefaultOutput)

/*!
 * \ingroup outputs
 * \brief Comma separated values output.
 * \author Josh Klontz \cite jklontz
 */
class csvOutput : public MatrixOutput
{
    Q_OBJECT

    ~csvOutput()
    {
        if (file.isNull() || targetFiles.isEmpty() || queryFiles.isEmpty()) return;
        QStringList lines;
        lines.append("File," + targetFiles.names().join(","));
        for (int i=0; i<queryFiles.size(); i++) {
            QStringList words;
            for (int j=0; j<targetFiles.size(); j++)
                words.append(toString(i,j));
            lines.append(queryFiles[i].name+","+words.join(","));
        }
        QtUtils::writeFile(file, lines);
    }
};

BR_REGISTER(Output, csvOutput)

/*!
 * \ingroup outputs
 * \brief One score per row.
 * \author Josh Klontz \cite jklontz
 */
class meltOutput : public MatrixOutput
{
    Q_OBJECT

    ~meltOutput()
    {
        if (file.isNull() || targetFiles.isEmpty() || queryFiles.isEmpty()) return;
        const bool genuineOnly = file.contains("Genuine") && !file.contains("Impostor");
        const bool impostorOnly = file.contains("Impostor") && !file.contains("Genuine");

        QHash<QString,QVariant> args = file.localMetadata();
        args.remove("Genuine");
        args.remove("Impostor");

        QString keys; foreach (const QString &key, args.keys()) keys += "," + key;
        QString values; foreach (const QVariant &value, args.values()) values += "," + value.toString();

        QStringList lines;
        if (file.baseName() != "terminal") lines.append(QString("Query,Target,Mask,Similarity%1").arg(keys));
        QList<float> queryLabels = queryFiles.labels();
        QList<float> targetLabels = targetFiles.labels();
        for (int i=0; i<queryFiles.size(); i++) {
            for (int j=(selfSimilar ? i+1 : 0); j<targetFiles.size(); j++) {
                const bool genuine = queryLabels[i] == targetLabels[j];
                if ((genuineOnly && !genuine) || (impostorOnly && genuine)) continue;
                lines.append(QString("%1,%2,%3,%4%5").arg(queryFiles[i],
                                                          targetFiles[j],
                                                          QString::number(genuine),
                                                          QString::number(data.at<float>(i,j)),
                                                          values));
            }
        }

        QtUtils::writeFile(file, lines);
    }
};

BR_REGISTER(Output, meltOutput)

/*!
 * \ingroup outputs
 * \brief \ref simmat output.
 * \author Josh Klontz \cite jklontz
 */
class mtxOutput : public MatrixOutput
{
    Q_OBJECT

    ~mtxOutput()
    {
        if (file.isNull() || targetFiles.isEmpty() || queryFiles.isEmpty()) return;
        BEE::writeSimmat(data, file.name);
    }
};

BR_REGISTER(Output, mtxOutput)

/*!
 * \ingroup outputs
 * \brief Rank retrieval output.
 * \author Josh Klontz \cite jklontz Scott Klum \cite sklum
 */
class rrOutput : public MatrixOutput
{
    Q_OBJECT

    ~rrOutput()
    {
        if (file.isNull() || targetFiles.isEmpty() || queryFiles.isEmpty()) return;
        const int limit = file.getInt("limit", 20);
        const bool byLine = file.getBool("byLine");
        const float threshold = file.getFloat("threshold", -std::numeric_limits<float>::max());

        QStringList lines;
        for (int i=0; i<queryFiles.size(); i++) {
            QStringList files;
            if (!byLine) files.append(queryFiles[i]);

            typedef QPair<float,int> Pair;
            foreach (const Pair &pair, Common::Sort(OpenCVUtils::matrixToVector(data.row(i)), true, limit)) {
                if (pair.first < threshold) break;
                File target = targetFiles[pair.second];
                target.set("Score", QString::number(pair.first));
                files.append(target.flat());
            }

            lines.append(files.join(byLine ? "\n" : ","));
        }

        QtUtils::writeFile(file, lines);
    }
};

BR_REGISTER(Output, rrOutput)

/*!
 * \ingroup outputs
 * \brief Text file output.
 * \author Josh Klontz \cite jklontz
 */
class txtOutput : public MatrixOutput
{
    Q_OBJECT

    ~txtOutput()
    {
        if (file.isNull() || targetFiles.isEmpty() || queryFiles.isEmpty()) return;
        QStringList lines;
        foreach (const File &file, queryFiles)
            lines.append(file.name + " " + file.subject());
        QtUtils::writeFile(file, lines);
    }
};

BR_REGISTER(Output, txtOutput)

/*!
 * \ingroup outputs
 * \brief Output to the terminal.
 * \author Josh Klontz \cite jklontz
 */
class EmptyOutput : public MatrixOutput
{
    Q_OBJECT

    static QString bufferString(const QString &string, int length)
    {
        if (string.size() >= length)
            return string.left(length);
        QString buffer; buffer.fill(' ', length-string.size());
        return string+buffer;
    }

    ~EmptyOutput()
    {
        if (targetFiles.isEmpty() || queryFiles.isEmpty()) return;
        QString result;
        if ((queryFiles.size() == 1) && (targetFiles.size() == 1)) {
            result = toString(0,0) + "\n";
        } else {
            const int CELL_SIZE = 12;

            result = bufferString(" ", CELL_SIZE) + " ";
            foreach (const QString &targetName, targetFiles.names())
                result += bufferString(targetName, CELL_SIZE) + " ";
            result += "\n";

            for (int i=0; i<queryFiles.size(); i++) {
                result += bufferString(queryFiles[i].name, CELL_SIZE) + " ";
                for (int j=0; j<targetFiles.size(); j++)
                    result += bufferString(toString(i,j), CELL_SIZE) + " ";
                result += "\n";
            }
        }

        printf("%s", qPrintable(result));
    }
};

BR_REGISTER(Output, EmptyOutput)

/*!
 * \ingroup outputs
 * \brief Outputs highest ranked matches with scores.
 * \author Scott Klum \cite sklum
 */
class rankOutput : public MatrixOutput
{
    Q_OBJECT

    ~rankOutput()
    {
        if (targetFiles.isEmpty() || queryFiles.isEmpty()) return;

        QList<int> ranks;
        QList<double> scores;
        QStringList lines;

        for (int i=0; i<queryFiles.size(); i++) {
            typedef QPair<float,int> Pair;
            int rank = 1;
            foreach (const Pair &pair, Common::Sort(OpenCVUtils::matrixToVector(data.row(i)), true)) {
                if(targetFiles[pair.second].label() == queryFiles[i].label()) {
                    ranks.append(rank);
                    scores.append(pair.first);
                    break;
                }
                rank++;
            }
        }

        typedef QPair<int,int> RankPair;
        foreach (const RankPair &pair, Common::Sort(ranks, false))
            lines.append(queryFiles[pair.second].name + " " + QString::number(pair.first) + " " + QString::number(scores[pair.second]) + " " + targetFiles[pair.second].name);

        QtUtils::writeFile(file, lines);
    }
};

BR_REGISTER(Output, rankOutput)

/*!
 * \ingroup outputs
 * \brief The highest scoring matches.
 * \author Josh Klontz \cite jklontz
 */
class tailOutput : public Output
{
    Q_OBJECT

    struct Comparison
    {
        br::File query, target;
        float value;

        Comparison(const br::File &_query, const br::File &_target, float _value)
            : query(_query), target(_target), value(_value) {}

        QString toString(bool args) const
        {
            return QString::number(value) + "," + (args ? target.flat() : (QString)target) + "," + (args ? query.flat() : (QString)query);
        }

        bool operator<(const Comparison &other) const
        {
            return value < other.value;
        }
    };

    float threshold;
    int atLeast, atMost;
    bool args;
    float lastValue;
    QList<Comparison> comparisons;
    QMutex comparisonsLock;

    ~tailOutput()
    {
        if (file.isNull() || comparisons.isEmpty()) return;
        QStringList lines; lines.reserve(comparisons.size()+1);
        lines.append("Value,Target,Query");
        foreach (const Comparison &duplicate, comparisons)
            lines.append(duplicate.toString(args));
        QtUtils::writeFile(file, lines);
    }

    void initialize(const FileList &targetFiles, const FileList &queryFiles)
    {
        Output::initialize(targetFiles, queryFiles);
        threshold = file.getFloat("threshold", -std::numeric_limits<float>::max());
        atLeast = file.getInt("atLeast", 1);
        atMost = file.getInt("atMost", std::numeric_limits<int>::max());
        args = file.getBool("args");
        lastValue = -std::numeric_limits<float>::max();
    }

    void set(float value, int i, int j)
    {
        // Return early for self similar matrices
        if (selfSimilar && (i <= j)) return;

        // Consider only values passing the criteria
        if ((value < threshold) && (value <= lastValue) && (comparisons.size() >= atLeast))
            return;

        comparisonsLock.lock();
        if (comparisons.isEmpty() || (value < comparisons.last().value)) {
            // Special cases
            comparisons.append(Comparison(queryFiles[i], targetFiles[j], value));
        } else {
            // General case
            for (int k=0; k<comparisons.size(); k++) {
                if (comparisons[k].value < value) {
                    comparisons.insert(k, Comparison(queryFiles[i], targetFiles[j], value));
                    break;
                }
            }
        }

        while (comparisons.size() > atMost)
            comparisons.removeLast();
        while ((comparisons.size() > atLeast) && (comparisons.last().value < threshold))
            comparisons.removeLast();
        lastValue = comparisons.last().value;
        comparisonsLock.unlock();
    }
};

BR_REGISTER(Output, tailOutput)

/*!
 * \ingroup outputs
 * \brief The highest scoring matches.
 * \author Josh Klontz \cite jklontz
 */
class bestOutput : public Output
{
    Q_OBJECT

    typedef QPair< float, QPair<int, int> > BestMatch;
    QList<BestMatch> bestMatches;

    ~bestOutput()
    {
        if (file.isNull() || bestMatches.isEmpty()) return;
        qSort(bestMatches);
        QStringList lines; lines.reserve(bestMatches.size()+1);
        lines.append("Value,Target,Query");
        for (int i=bestMatches.size()-1; i>=0; i--)
            lines.append(QString::number(bestMatches[i].first) + "," + targetFiles[bestMatches[i].second.second] + "," + queryFiles[bestMatches[i].second.first]);
        QtUtils::writeFile(file, lines);
    }

    void initialize(const FileList &targetFiles, const FileList &queryFiles)
    {
        Output::initialize(targetFiles, queryFiles);
        bestMatches.reserve(queryFiles.size());
        for (int i=0; i<queryFiles.size(); i++)
            bestMatches.append(BestMatch(-std::numeric_limits<float>::max(), QPair<int,int>(-1, -1)));
    }

    void set(float value, int i, int j)
    {
        static QMutex lock;

        // Return early for self similar matrices
        if (selfSimilar && (i == j)) return;

        if (value > bestMatches[i].first) {
            lock.lock();
            if (value > bestMatches[i].first)
                bestMatches[i] = BestMatch(value, QPair<int,int>(i,j));
            lock.unlock();
        }
    }
};

BR_REGISTER(Output, bestOutput)

/*!
 * \ingroup outputs
 * \brief Score histogram.
 * \author Josh Klontz \cite jklontz
 */
class histOutput : public Output
{
    Q_OBJECT

    float min, max, step;
    QVector<int> bins;

    ~histOutput()
    {
        if (file.isNull() || bins.isEmpty()) return;
        QStringList counts;
        foreach (int count, bins)
            counts.append(QString::number(count));
        const QString result = counts.join(",");
        QtUtils::writeFile(file, result);
    }

    void initialize(const FileList &targetFiles, const FileList &queryFiles)
    {
        Output::initialize(targetFiles, queryFiles);
        min = file.getFloat("min", -5);
        max = file.getFloat("max", 5);
        step = file.getFloat("step", 0.1);
        bins = QVector<int>((max-min)/step, 0);
    }

    void set(float value, int i, int j)
    {
        (void) i;
        (void) j;
        if ((value < min) || (value >= max)) return;
        bins[(value-min)/step]++; // This should technically be locked to ensure atomic increment
    }
};

BR_REGISTER(Output, histOutput)

} // namespace br

#include "output.moc"
