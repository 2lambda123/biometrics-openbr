0.3.0 - ??/??/??
================
* YouTubeFacesDBTransform implements Dr. Wolf's experimental protocol
* NEC3 refactored

0.2.0 - 2/23/13
===============
* FaceRecognition new distance metric
  - 0 to 1 range indicating match probability
* Qt 4.8 -> Qt 5.0
* Cleaner plots generated with 'br -plot'
* Stasm and FLandmark wrappers
* Improved demographic filtering speed
  - br::Context::demographicFilters -> br::Context::filters
  - MetadataDistance -> FilterDistance
* PipeDistance
* ImpostorUniquenessMeasureTransform
* MatchProbabilityDistance
* CrossValidation framework
  - br::Context::crossValidate
  - CrossValidationTransform
  - CrossValidationDistance

0.1.0 - 1/27/13
===============
First official release!
