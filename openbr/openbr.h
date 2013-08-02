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

#ifndef __OPENBR_H
#define __OPENBR_H

#include <openbr/openbr_export.h>

#ifdef __cplusplus
extern "C" {
#endif

 /*!
 * \defgroup c_sdk C SDK
 * \brief High-level API for running algorithms and evaluating results.
 *
 * In order to provide a high-level interface that is usable from the command line and callable from other programming languages,
 * the API is designed to operate at the "file system" level.
 * In other words, arguments to many functions are file paths that specify either a source of input or a desired output.
 * File extensions are relied upon to determine \em how files should be interpreted in the context of the function being called.
 * The \ref cpp_plugin_sdk should be used if more fine-grained control is required.
 *
 * \code
 * #include <openbr/openbr.h>
 * \endcode
 * <a href="http://www.cmake.org/">CMake</a> developers may wish to use <tt>share/openbr/cmake/OpenBRConfig.cmake</tt>.
 *
 * \section managed_return_value Managed Return Value
 * Memory for <tt>const char*</tt> return values is managed internally and guaranteed until the next call to the function.
 *
 * \section python_api Python API
 * A Python API is available via <a href="http://www.swig.org/">SWIG</a>.
 * \code
 * $ ls include/br/python
 * \endcode
 *
 * \section java_api Java API
 * A Java API is available via <a href="http://www.swig.org/">SWIG</a>.
 * \code
 * $ ls include/br/java
 * \endcode
 *
 * \section examples Examples
 * - \ref c_face_recognition_evaluation
 *
 * \subsection c_face_recognition_evaluation Face Recognition Evaluation
 * \ref cli_face_recognition_evaluation "Command Line Interface Equivalent"
 * \snippet app/examples/face_recognition_evaluation.cpp face_recognition_evaluation
 */

/*!
 * \addtogroup c_sdk
 *  @{
 */

/*!
 * \brief Wraps br::Context::about()
 * \note \ref managed_return_value
 * \see br_version
 */
BR_EXPORT const char *br_about();

/*!
 * \brief Wraps br::Cat()
 */
BR_EXPORT void br_cat(int num_input_galleries, const char *input_galleries[], const char *output_gallery);

/*!
 * \brief Clusters one or more similarity matrices into a list of subjects.
 *
 * A similarity matrix is a type of br::Output. The current clustering algorithm is a simplified implementation of \cite zhu11.
 * \param num_simmats Size of \c simmats.
 * \param simmats Array of \ref simmat composing one large self-similarity matrix arranged in row major order.
 * \param aggressiveness The higher the aggressiveness the larger the clusters. Suggested range is [0,10].
 * \param csv The cluster results file to generate. Results are stored one row per cluster and use gallery indices.
 */
BR_EXPORT void br_cluster(int num_simmats, const char *simmats[], float aggressiveness, const char *csv);

/*!
 * \brief Combines several equal-sized mask matrices.
 * \param num_input_masks Size of \c input_masks
 * \param input_masks Array of \ref mask to combine.
 *                    All matrices must have the same dimensions.
 * \param output_mask The file to contain the resulting \ref mask.
 * \param method Either:
 *  - \c And - Ignore comparison if \em any input masks ignore.
 *  - \c Or - Ignore comparison if \em all input masks ignore.
 * \note A comparison may not be simultaneously identified as both a genuine and an impostor by different input masks.
 * \see br_make_mask
 */
BR_EXPORT void br_combine_masks(int num_input_masks, const char *input_masks[], const char *output_mask, const char *method);

/*!
 * \brief Compares each template in the query gallery to each template in the target gallery.
 * \param target_gallery The br::Gallery file whose templates make up the columns of the output.
 * \param query_gallery The br::Gallery file whose templates make up the rows of the output.
 *                      A value of '.' reuses the target gallery as the query gallery.
 * \param output Optional br::Output file to contain the results of comparing the templates.
 *               The default behavior is to print scores to the terminal.
 * \see br_enroll
 */
BR_EXPORT void br_compare(const char *target_gallery, const char *query_gallery, const char *output = "");

/*!
 * \brief Wraps br::Convert()
 */
BR_EXPORT void br_convert(const char *file_type, const char *input_file, const char *output_file);

/*!
 * \brief Constructs template(s) from an input.
 * \param input The br::Input set of images to enroll.
 * \param gallery The br::Gallery file to contain the enrolled templates.
 *                By default the gallery will be held in memory and \em input can used as a gallery in \ref br_compare.
 * \see br_enroll_n
 */
BR_EXPORT void br_enroll(const char *input, const char *gallery = "");

/*!
 * \brief Convenience function for enrolling multiple inputs.
 * \see br_enroll
 */
BR_EXPORT void br_enroll_n(int num_inputs, const char *inputs[], const char *gallery = "");

/*!
 * \brief Creates a \c .csv file containing performance metrics from evaluating the similarity matrix using the mask matrix.
 * \param simmat The \ref simmat to use.
 * \param mask The \ref mask to use.
 * \param csv Optional \c .csv file to contain performance metrics.
 * \return True accept rate at a false accept rate of one in one thousand.
 * \see br_plot
 */
BR_EXPORT float br_eval(const char *simmat, const char *mask, const char *csv = "");

/*!
 * \brief Evaluates and prints classification accuracy to terminal.
 * \param predicted_gallery The predicted br::Gallery.
 * \param truth_gallery The ground truth br::Gallery.
 */
BR_EXPORT void br_eval_classification(const char *predicted_gallery, const char *truth_gallery, const char * predicted_property="", const char * truth_property="");

/*!
 * \brief Evaluates and prints clustering accuracy to the terminal.
 * \param csv The cluster results file.
 * \param gallery The br::Gallery used to generate the \ref simmat that was clustered.
 * \see br_cluster
 */
BR_EXPORT void br_eval_clustering(const char *csv, const char *gallery);

/*!
 * \brief Evaluates and prints detection accuracy to terminal.
 * \param predicted_gallery The predicted br::Gallery.
 * \param truth_gallery The ground truth br::Gallery.
 * \param csv Optional \c .csv file to contain performance metrics.
 * \return Average detection bounding box overlap.
 */
BR_EXPORT float br_eval_detection(const char *predicted_gallery, const char *truth_gallery, const char *csv = "");

/*!
 * \brief Evaluates regression accuracy to disk.
 * \param predicted_gallery The predicted br::Gallery.
 * \param truth_gallery The ground truth br::Gallery.
 */
BR_EXPORT void br_eval_regression(const char *predicted_gallery, const char *truth_gallery, const char * predicted_property="", const char * truth_property="");

/*!
 * \brief Wraps br::Context::finalize()
 * \see br_initialize
 */
BR_EXPORT void br_finalize();

/*!
 * \brief Perform score level fusion on similarity matrices.
 * \param num_input_simmats Size of \em input_simmats.
 * \param input_simmats Array of \ref simmat. All simmats must have the same dimensions.
 * \param mask \ref mask used to indicate which, if any, values to ignore.
 * \param normalization Valid options are:
 *          - \c None - No score normalization.
 *          - \c MinMax - Scores normalized to [0,1].
 *          - \c ZScore - Scores normalized to a standard normal curve.
 * \param fusion Valid options are:
 *          - \c Min - Uses the minimum score.
 *          - \c Max - Uses the maximum score.
 *          - \c Sum - Sums the scores. Sums can also be weighted: <tt>SumW1:W2:...:Wn</tt>.
 *          - \c Replace - Replaces scores in the first matrix with scores in the second matrix when the mask is set.
 * \param output_simmat \ref simmat to contain the fused scores.
 */
BR_EXPORT void br_fuse(int num_input_simmats, const char *input_simmats[], const char *mask,
                       const char *normalization, const char *fusion, const char *output_simmat);

/*!
 * \brief Wraps br::Context::initialize()
 * \see br_finalize
 */
BR_EXPORT void br_initialize(int &argc, char *argv[], const char *sdk_path = "");

/*!
 * \brief Wraps br::IsClassifier()
 */
BR_EXPORT bool br_is_classifier(const char *algorithm);

/*!
 * \brief Constructs a \ref mask from target and query inputs.
 * \param target_input The target br::Input.
 * \param query_input The query br::Input.
 * \param mask The file to contain the resulting \ref mask.
 * \see br_combine_masks
 */
BR_EXPORT void br_make_mask(const char *target_input, const char *query_input, const char *mask);

/*!
 * \brief Returns the most recent line sent to stderr.
 * \note \ref managed_return_value
 * \see br_progress br_time_remaining
 */
BR_EXPORT const char *br_most_recent_message();

/*!
 * \brief Returns names and parameters for the requested objects.
 *
 * Each object is \c \\n seperated. Arguments are seperated from the object name with a \c \\t.
 * \param abstractions Regular expression of the abstractions to search.
 * \param implementations Regular expression of the implementations to search.
 * \param parameters Include parameters after object name.
 * \note \ref managed_return_value
 * \note This function uses Qt's <a href="http://doc.qt.digia.com/stable/qregexp.html">QRegExp</a> syntax.
 */
BR_EXPORT const char *br_objects(const char *abstractions = ".*", const char *implementations = ".*", bool parameters = true);

/*!
 * \brief Renders recognition performance figures for a set of <tt>.csv</tt> files created by \ref br_eval.
 *
 * In order of their output, the figures are:
 * -# Metadata table
 * -# Receiver Operating Characteristic (ROC)
 * -# Detection Error Tradeoff (DET)
 * -# Score Distribution (SD) histogram
 * -# True Accept Rate Bar Chart (BC)
 * -# Cumulative Match Characteristic (CMC)
 * -# Error Rate (ERR) curve
 *
 * Two files will be created:
 * - <i>destination</i><tt>.R</tt> which is the auto-generated R script used to render the figures.
 * - <i>destination</i><tt>.pdf</tt> which has all of the figures in one file multi-page file.
 *
 * \param num_files Number of <tt>.csv</tt> files.
 * \param files <tt>.csv</tt> files created using \ref br_eval.
 * \param destination Basename for the resulting figures.
 * \param show Open <i>destination</i>.pdf using the system's default PDF viewer.
 * \return Returns \c true on success. Returns false on a failure to compile the figures due to a missing, out of date, or incomplete \c R installation.
 * \note This function requires a current <a href="http://www.r-project.org/">R</a> installation with the following packages:
 * \code install.packages(c("ggplot2", "gplots", "reshape", "scales")) \endcode
 * \see br_eval
 */
BR_EXPORT bool br_plot(int num_files, const char *files[], const char *destination, bool show = false);

/*!
 * \brief Renders detection performance figures for a set of <tt>.csv</tt> files created by \ref br_eval_detection.
 *
 * In order of their output, the figures are:
 * -# Discrete Receiver Operating Characteristic (DiscreteROC)
 * -# Continuous Receiver Operating Characteristic (ContinuousROC)
 * -# Discrete Precision Recall (DiscretePR)
 * -# Continuous Precision Recall (ContinuousPR)
 * -# Bounding Box Overlap Histogram (Overlap)
 *
 * Detection accuracy is measured with <i>overlap fraction = bounding box intersection / union</i>.
 * When computing <i>discrete</i> curves, an overlap >= 0.5 is considered a true positive, otherwise it is considered a false negative.
 * When computing <i>continuous</i> curves, true positives and false negatives are measured fractionally as <i>overlap</i> and <i>1-overlap</i> respectively.
 *
 * \see br_plot
 */
BR_EXPORT bool br_plot_detection(int num_files, const char *files[], const char *destination, bool show = false);

/*!
 * \brief Renders metadata figures for a set of <tt>.csv</tt> files with specified columns.
 *
 * Several files will be created:
 * - <tt>PlotMetadata.R</tt> which is the auto-generated R script used to render the figures.
 * - <tt>PlotMetadata.pdf</tt> which has all of the figures in one file (convenient for attaching in an email).
 * - <i>column</i><tt>.pdf</tt>, ..., <i>column</i><tt>.pdf</tt> which has each figure in a separate file (convenient for including in a presentation).
 *
 * \param num_files Number of <tt>.csv</tt> files.
 * \param files <tt>.csv</tt> files created by enrolling templates to <tt>.csv</tt> metadata files.
 * \param columns ';' seperated list of columns to plot.
 * \param show Open <tt>PlotMetadata.pdf</tt> using the system's default PDF viewer.
 * \return See \ref br_plot
 */
BR_EXPORT bool br_plot_metadata(int num_files, const char *files[], const char *columns, bool show = false);

/*!
 * \brief Wraps br::Context::progress()
 * \see br_most_recent_message br_time_remaining
 */
BR_EXPORT float br_progress();

/*!
 * \brief Read and parse arguments from a named pipe.
 *
 * Used by the \ref cli to implement \c -daemon, generally not useful otherwise.
 * Guaranteed to return at least one argument.
 * \param pipe Pipe name
 * \param[out] argc argument count
 * \param[out] argv argument list
 * \note \ref managed_return_value
 */
BR_EXPORT void br_read_pipe(const char *pipe, int *argc, char ***argv);

/*!
 * \brief Wraps br::Context::scratchPath()
 * \note \ref managed_return_value
 * \see br_version
 */
BR_EXPORT const char *br_scratch_path();

/*!
 * \brief Returns the full path to the root of the SDK.
 * \note \ref managed_return_value
 * \see br_initialize
 */
BR_EXPORT const char *br_sdk_path();

/*!
 * \brief Retrieve the target and query inputs in the BEE matrix header.
 * \param matrix The BEE matrix file to modify
 * \param[out] target_gallery The matrix target
 * \param[out] query_gallery The matrix query
 * \note \ref managed_return_value
 * \see br_set_header
 */
BR_EXPORT void br_get_header(const char *matrix, const char **target_gallery, const char **query_gallery);

/*!
 * \brief Update the target and query inputs in the BEE matrix header.
 * \param matrix The BEE matrix file to modify
 * \param target_gallery The matrix target
 * \param query_gallery The matrix query
 * \see br_get_header
 */
BR_EXPORT void br_set_header(const char *matrix, const char *target_gallery, const char *query_gallery);

/*!
 *\brief Wraps br::Context::setProperty()
 */
BR_EXPORT void br_set_property(const char *key, const char *value);

/*!
 * \brief Wraps br::Context::timeRemaining()
 * \see br_most_recent_message br_progress
 */
BR_EXPORT int br_time_remaining();

/*!
 * \brief Trains the br::Transform and br::Comparer on the input.
 * \param input The br::Input set of images to train on.
 * \param model Optional string specifying the binary file to serialize training results to.
 *              The trained algorithm can be recovered by using this file as the algorithm.
 *              By default the trained algorithm will not be serialized to disk.
 * \see br_train_n
 */
BR_EXPORT void br_train(const char *input, const char *model = "");

/*!
 * \brief Convenience function for training on multiple inputs.
 * \see br_train
 */
BR_EXPORT void br_train_n(int num_inputs, const char *inputs[], const char *model = "");

/*!
 * \brief Wraps br::Context::version()
 * \note \ref managed_return_value
 * \see br_about br_scratch_path
 */
BR_EXPORT const char *br_version();

/*! @}*/

#ifdef __cplusplus
}
#endif

#endif // __OPENBR_H
