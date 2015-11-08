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

#include <openbr/plugins/openbr_internal.h>
#include <openbr/core/opencvutils.h>
#include <openbr/core/qtutils.h>

#include <opencv2/imgproc/imgproc.hpp>

using namespace cv;

namespace br
{

/*!
 * \ingroup transforms
 * \brief Sliding Window Framework for object detection. Performs an exhaustive search of an image by sliding a window of a given size around the image and then resizing the image and repeating until terminating conditions are met.
 * \author Jordan Cheney \cite jcheney
 * \author Scott Klum \cite sklum
 * \br_property Classifier* classifier The classifier that determines if a given window is a positive or negative sample. The size of the window is determined using the classifiers *windowSize* method.
 * \br_property int minSize The smallest sized object to detect in pixels
 * \br_property int maxSize The largest sized object to detect in pixels. A negative value will set maxSize == image size
 * \br_property float scaleFactor The factor to scale the image by during each resize.
 * \br_property float confidenceThreshold A threshold for positive detections. Positive detections returned by the classifier that have confidences below this threshold are considered negative detections.
 * \br_property float eps Parameter for non-maximum supression
 * \br_property bool toRectList If true, then append all detection to the metadata rects. If false, create a new template for every detectoin
 */
class SlidingWindowTransform : public MetaTransform
{
    Q_OBJECT

    Q_PROPERTY(br::Classifier* classifier READ get_classifier WRITE set_classifier RESET reset_classifier STORED false)

    Q_PROPERTY(int minSize READ get_minSize WRITE set_minSize RESET reset_minSize STORED false)
    Q_PROPERTY(int maxSize READ get_maxSize WRITE set_maxSize RESET reset_maxSize STORED false)
    Q_PROPERTY(float scaleFactor READ get_scaleFactor WRITE set_scaleFactor RESET reset_scaleFactor STORED false)
    Q_PROPERTY(float confidenceThreshold READ get_confidenceThreshold WRITE set_confidenceThreshold RESET reset_confidenceThreshold STORED false)
    Q_PROPERTY(float eps READ get_eps WRITE set_eps RESET reset_eps STORED false)
    Q_PROPERTY(float minNeighbors READ get_minNeighbors WRITE set_minNeighbors RESET reset_minNeighbors STORED false)
    Q_PROPERTY(bool group READ get_group WRITE set_group RESET reset_group STORED false)
    Q_PROPERTY(bool toRectList READ get_toRectList WRITE set_toRectList RESET reset_toRectList STORED false)
    BR_PROPERTY(br::Classifier*, classifier, NULL)
    BR_PROPERTY(int, minSize, 20)
    BR_PROPERTY(int, maxSize, -1)
    BR_PROPERTY(float, scaleFactor, 1.2)
    BR_PROPERTY(float, confidenceThreshold, 10)
    BR_PROPERTY(float, eps, 0.2)
    BR_PROPERTY(int, minNeighbors, 3)
    BR_PROPERTY(bool, group, true)
    BR_PROPERTY(bool, toRectList, false)

    void train(const TemplateList &data)
    {
        classifier->train(data);
    }

    void project(const Template &src, Template &dst) const
    {
        TemplateList temp;
        project(TemplateList() << src, temp);
        if (!temp.isEmpty()) dst = temp.first();
    }

    void project(const TemplateList &src, TemplateList &dst) const
    {
        foreach (const Template &t, src) {
            Template out = t;

            // As a special case, skip detection if the appropriate metadata already exists
            if (t.file.contains("Face")) {
                Template u = t;
                u.file.setRects(QList<QRectF>() << t.file.get<QRectF>("Face"));
                u.file.set("Confidence", t.file.get<float>("Confidence", 1));
                dst.append(u);
                continue;
            }

            const bool enrollAll = t.file.getBool("enrollAll");

            // Mirror the behavior of ExpandTransform in the special case
            // of an empty template.
            if (t.empty() && !enrollAll) {
                dst.append(t);
                continue;
            }

            // SlidingWindow assumes that all matricies in a template represent
            // different channels of the same image!
            const Size imageSize = t.m().size();
            const int minSize = t.file.get<int>("MinSize", this->minSize);
            QList<Rect> rects;
            QList<float> confidences;

            int dx, dy;
            const Size originalWindowSize = classifier->windowSize(&dx, &dy);

            for (double factor = 1; ; factor *= scaleFactor) {
                const Size windowSize(cvRound(originalWindowSize.width*factor), cvRound(originalWindowSize.height*factor));
                const Size scaledImageSize(cvRound(imageSize.width/factor), cvRound(imageSize.height/factor));
                const Size processingRectSize(scaledImageSize.width - originalWindowSize.width, scaledImageSize.height - originalWindowSize.height);

                if (processingRectSize.width <= 0 || processingRectSize.height <= 0)
                    break;
                if (windowSize.width < minSize || windowSize.height < minSize)
                    continue;

                Template rep(t.file);
                foreach (const Mat &m, t) {
                    Mat scaledImage;
                    resize(m, scaledImage, scaledImageSize, 0, 0, CV_INTER_LINEAR);
                    rep.append(scaledImage);
                }
                rep = classifier->preprocess(rep);

                // Pre-allocate the window to avoid constructing this every iteration
                Template window(t.file);
                for (int i=0; i<rep.size(); i++)
                    window.append(Mat());

                const int step = factor > 2.0 ? 1 : 2;
                for (int y = 0; y < processingRectSize.height; y += step) {
                    for (int x = 0; x < processingRectSize.width; x += step) {
                        for (int i=0; i<rep.size(); i++)
                            window[i] = rep[i](Rect(Point(x, y), Size(originalWindowSize.width + dx, originalWindowSize.height + dy))).clone();

                        float confidence = 0;
                        int result = classifier->classify(window, false, &confidence);

                        if (result == 1) {
                            rects.append(Rect(cvRound(x*factor), cvRound(y*factor), windowSize.width, windowSize.height));
                            confidences.append(confidence);
                        }

                        // TODO: Add non ROC mode

                        if (result == 0)
                            x += step;
                    }
                }
            }

            if (group)
                OpenCVUtils::group(rects, confidences, confidenceThreshold, minNeighbors, eps);

            if (!enrollAll && rects.empty()) {
                rects.append(Rect(0, 0, imageSize.width, imageSize.height));
                confidences.append(-std::numeric_limits<float>::max());
            }

            for (int j=0; j<rects.size(); j++) {
                if (toRectList) {
                    out.file.appendRect(rects[j]);
                } else {
                    Template u = t;
                    u.file.set("Confidence", confidences[j]);
                    const QRectF rect = OpenCVUtils::fromRect(rects[j]);
                    u.file.appendRect(rect);
                    u.file.set("Face", rect);
                    dst.append(u);
                }
            }

            if (toRectList) 
                dst.append(out);
        }
    }

    void load(QDataStream &stream)
    {
        classifier->load(stream);
    }

    void store(QDataStream &stream) const
    {
        classifier->store(stream);
    }
};

BR_REGISTER(Transform, SlidingWindowTransform)

} // namespace br

#include "imgproc/slidingwindow.moc"
