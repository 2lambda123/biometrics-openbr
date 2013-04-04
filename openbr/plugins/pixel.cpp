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

#include "openbr_internal.h"

using namespace cv;

namespace br
{

/*!
 * \ingroup transforms
 * \brief Treat each pixel as a classification task
 * \author E. Taborsky \cite mmtaborsky
 */
class PerPixelClassifierTransform : public MetaTransform
{
    Q_OBJECT
    Q_PROPERTY(br::Transform* transform READ get_transform WRITE set_transform RESET reset_transform)
    Q_PROPERTY(int pixels READ get_pixels WRITE set_pixels RESET reset_pixels STORED false)
    Q_PROPERTY(int orient READ get_orient WRITE set_orient RESET reset_orient STORED false)
    BR_PROPERTY(br::Transform*, transform, NULL)
    BR_PROPERTY(int, pixels, 10000)
    BR_PROPERTY(bool, orient, false)

    /*
      Bins:
      |4|3|2|
      |5| |1|
      |6|7|8|
      */

    QList<float> shift(int n, QList<float> &src) const
    {
        for (int i = 0; i < n; i++){ // Equivalent to src.append(src.takeFirst()) ?
            src.append(src.at(i));
            src.removeFirst();
        }
        return src;
    }

    void rotate(Template &src, Template &dst) const
    {
        int images =  (src.m().cols)/9;
        dst = src;
        for (int i = 0; i < images; i++){
            double a = src.m().at<float>(7+(i*9)); //top
            double b = src.m().at<float>(1+(i*9)); //bottom
            double c = src.m().at<float>(5+(i*9)); //right
            double d = src.m().at<float>(3+(i*9)); //left
            double orientation = atan2((a-b),(c-d));
            int bin;
            if (orientation > 0){
                 bin = ((orientation/CV_PI)*4.0 +.5);
            } else {
                 bin = 8.0 + ((orientation/CV_PI)*4.0 + .5);
            }

            // put things in an order that makes sense to rotate
            // blugh
            QList<float> orderedList;
            QList<float> rotatedList;
            orderedList.insert(0, src.m().at<float>(3+(i*9)));
            orderedList.insert(1, src.m().at<float>(6+(i*9)));
            orderedList.insert(2, src.m().at<float>(7+(i*9)));
            orderedList.insert(3, src.m().at<float>(8+(i*9)));
            orderedList.insert(4, src.m().at<float>(5+(i*9)));
            orderedList.insert(5, src.m().at<float>(2+(i*9)));
            orderedList.insert(6, src.m().at<float>(1+(i*9)));
            orderedList.insert(7, src.m().at<float>(0+(i*9)));

            rotatedList = shift(bin, orderedList);

            dst.m().at<float>(0+(i*9)) = rotatedList.at(7);
            dst.m().at<float>(1+(i*9)) = rotatedList.at(6);
            dst.m().at<float>(2+(i*9)) = rotatedList.at(5);
            dst.m().at<float>(3+(i*9)) = rotatedList.at(0);
            dst.m().at<float>(4+(i*9)) = src.m().at<float>(4+(i*9)); // middle pixel not in orderedList
            dst.m().at<float>(5+(i*9)) = rotatedList.at(4);
            dst.m().at<float>(6+(i*9)) = rotatedList.at(1);
            dst.m().at<float>(7+(i*9)) = rotatedList.at(2);
            dst.m().at<float>(8+(i*9)) = rotatedList.at(3);
        }
    }

    void train(const TemplateList &trainingSet)
    {
        TemplateList pixelTemplates = TemplateList();
        const int length = trainingSet.length();
        int pixelsPerImage = pixels/length;

        for (int i=0; i < length; i++){
            Template src = trainingSet.at(i);

            const int mats = src.length();
            const int rows = src.m().rows;
            const int cols = src.m().cols;

            RNG &rng = theRNG();
            TemplateList srcPixelTemplates;

            for (int m=0; m < pixelsPerImage; m++){
                int index = rng.uniform(0, rows*cols);
                Template temp = Template(src.file, cv::Mat(1, mats, CV_32F));
                float *ptemp = (float*)temp.m().ptr();
                for (int n=0; n < mats; n++){
                    uchar *psrc = src[n].ptr();
                    ptemp[n] = psrc[index];
                }
                cv::Mat labelMat = src.file.value("labels").value<cv::Mat>();
                uchar* plabel = labelMat.ptr();
                temp.file.setLabel(plabel[index]);

                if (orient){
                    Template rotated;
                    rotate(temp, rotated);
                    srcPixelTemplates.append(rotated);
                } else {
                    srcPixelTemplates.append(temp);
                }
            }
            pixelTemplates.append(srcPixelTemplates);
        }
        transform->train(pixelTemplates);
    }

    void project(const Template &src, Template &dst) const
    {
        const int mats = src.length();
        const int rows = src.m().rows;
        const int cols = src.m().cols;

        dst = src; // Do we really want to copy all the src matrices into dst?
        dst.merge(Template(src.file, cv::Mat(src.m().rows, src.m().cols, CV_32F)));
        float *pdst = (float*) dst.m().ptr();

        for (int r = 0; r < rows; r++){
            for (int c = 0; c < cols; c++){
                Template temp = Template(src.file, cv::Mat(1, mats, CV_32F));
                Template dstTemp = Template(src.file, cv::Mat(1, mats, CV_32F));

                for (int n=0; n < mats; n++){
                    const uchar *psrc = src[n].ptr();
                    float *ptemp = (float*)temp[0].ptr();
                    int index = r*cols + c;
                    ptemp[n] = psrc[index];
                }

                if (orient){
                    Template rotated = Template(src.file, cv::Mat(1, mats, CV_32F));
                    rotate(temp, rotated);
                    temp = rotated;
                }

                transform->project(temp,dstTemp);
                pdst[r*cols+c] = dstTemp.file.label();
            }
        }
    }
};

BR_REGISTER(Transform, PerPixelClassifierTransform)

/*!
 * \ingroup transforms
 * \brief Construct feature vectors of neighboring pixels
 * \author E. Taborsky \cite mmtaborsky
 */
class NeighborsTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        int rows = src.m().rows;
        int cols = src.m().cols;
        int mats = src.length();
        dst.file = src.file;

        for (int n = 0; n < mats; n++){ //each matrix, except the last one, will be turned into 9 matrices
            const uchar *psrc = src[n].ptr();
            for (int i = -1; i < 2; i++){
                for (int j = -1; j < 2; j++){ // these nine matrices are shifted versions of the original
                    cv::Mat mat = cv::Mat(rows, cols, CV_8UC1);
                    uchar *pmat = (uchar*)mat.ptr();
                    for (int r = 0; r < rows; r++){
                        for (int c = 0; c < cols; c++){
                            int index = r*cols+c;
                            int newIndex = index + i*cols + j;
                            if ((newIndex < 0) || (newIndex >= rows*cols)){
                                pmat[index] = psrc[index];
                            } else {
                                pmat[index] = psrc[newIndex];
                            }
                        }
                    }
                    dst.push_back(mat); //add mat to dst
                }
            }
        }
        dst.push_back(src.m()); // add the last matrix
    }
};

BR_REGISTER(Transform, NeighborsTransform)

/*!
 * \ingroup transforms
 * \brief To binary vector
 * \author E. Taborsky \cite mmtaborsky
 */
class ToBinaryVectorTransform : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(br::Transform* transform READ get_transform WRITE set_transform RESET reset_transform STORED false)
    Q_PROPERTY(int length READ get_length WRITE set_length RESET reset_length STORED false)
    BR_PROPERTY(br::Transform*, transform, NULL)
    BR_PROPERTY(int, length, -1)

    //needs to be updated..
    void project(const Template &src, Template &dst) const
    {

        dst = src;
        int mats = src.length();
        for (int i = 0; i < mats; i++){
            // Does this actually modify the data?
            dst[i]*(1.0/255.0); //scaling the input matrices to make the svm happier
        }
        for (int i = 0; i < length*(mats); i++){
            dst.prepend(Template(src.file, Mat::zeros(src.m().rows, src.m().cols, CV_8U)));
        }

        // original pixel values at the end

        Template transformed;
        transformed.file = src.file;
        transform->project(src, transformed);

        int rows = transformed.m().rows;
        int cols = transformed.m().cols;

        for (int i = 0; i < mats; i++){
            uchar *ptransformed = transformed[i].ptr();
            for (int r = 0; r < rows; r++){
                for (int c = 0; c < cols; c++){
                    uchar index = ptransformed[r*cols+c];
                    dst[index+(length*i)].at<uchar>(r,c) = 1;
                }
            }
        }
    }
};

BR_REGISTER(Transform, ToBinaryVectorTransform)

/*!
 * \ingroup transforms
 * \brief If "labels" is specified, makes the last matrix into metadata
 * \author E. Taborsky \cite mmtaborsky
 */

class ToMetadataTransform : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        dst = src;
        if (dst.file.contains("labels")){
            QVariant last = qVariantFromValue(dst.m());
            dst.file.set("labels", last);
            dst.pop_back();
        }
    }

};

BR_REGISTER(Transform, ToMetadataTransform)

} // namespace br

#include "pixel.moc"
