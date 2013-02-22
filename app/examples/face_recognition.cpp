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

/*!
 * \ingroup cli
 * \page cli_face_recognition Face Recognition
 * \ref cpp_face_recognition "C++ Equivalent"
 * \code
 * $ br -algorithm FaceRecognition \
 *      -compare ../data/MEDS/img/S354-01-t10_01.jpg ../data/MEDS/img/S354-02-t10_01.jpg \
 *      -compare ../data/MEDS/img/S354-01-t10_01.jpg ../data/MEDS/img/S386-04-t10_01.jpg
 * \endcode
 */

//! [face_recognition]
#include <openbr_plugin.h>

static void printTemplate(const br::Template &t)
{
    printf("%s eyes: (%d, %d) (%d, %d)\n",
           qPrintable(t.file.fileName()),
           t.file.getInt("Affine_0_X"), t.file.getInt("Affine_0_Y"),
           t.file.getInt("Affine_1_X"), t.file.getInt("Affine_1_Y"));
}

int main(int argc, char *argv[])
{
    br::Context::initialize(argc, argv);

    // Retrieve classes for enrolling and comparing templates using the FaceRecognition algorithm
    QSharedPointer<br::Transform> transform = br::Transform::fromAlgorithm("FaceRecognition");
    QSharedPointer<br::Distance> distance = br::Distance::fromAlgorithm("FaceRecognition");

    // Initialize templates
    br::Template queryA("../data/MEDS/img/S354-01-t10_01.jpg");
    br::Template queryB("../data/MEDS/img/S386-04-t10_01.jpg");
    br::Template target("../data/MEDS/img/S354-02-t10_01.jpg");

    // Enroll templates
    queryA >> *transform;
    queryB >> *transform;
    target >> *transform;

    printTemplate(queryA);
    printTemplate(queryB);
    printTemplate(target);

    // Compare templates
    float comparisonA = distance->compare(target, queryA);
    float comparisonB = distance->compare(target, queryB);

    printf("Genuine match score: %.3f\n", comparisonA); // Scores above 1 are strong matches
    printf("Impostor match score: %.3f\n", comparisonB); // Scores below 0.5 are strong non-matches

    br::Context::finalize();
    return 0;
}
//! [face_recognition]
