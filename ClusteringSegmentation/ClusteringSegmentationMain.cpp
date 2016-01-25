//
//  main.cpp
//  ClusteringSegmentation
//
//  Created by Mo DeJong on 12/30/15.
//  Copyright © 2015 helpurock. All rights reserved.
//

// clusteringsegmentation IMAGE TAGS_IMAGE
//
// This logic reads input pixels from an image and segments the image into different connected
// areas based on growing area of alike pixels. A set of pixels is determined to be alike
// if the pixels are near to each other in terms of 3D space via a fast clustering method.
// The TAGS_IMAGE output file is written with alike pixels being defined as having the same
// tag color.

#include <opencv2/opencv.hpp>

#include "ClusteringSegmentation.hpp"

#include "Superpixel.h"
#include "SuperpixelEdge.h"
#include "SuperpixelImage.h"

#include "OpenCVUtil.h"
#include "Util.h"

#include "quant_util.h"
#include "DivQuantHeader.h"

#include "MergeSuperpixelImage.h"

#include "srm.h"

#include "peakdetect.h"

#include "Util.h"

#include <stack>

using namespace cv;
using namespace std;

bool clusteringCombine(Mat &inputImg, Mat &resultImg);

int main(int argc, const char** argv) {
  const char *inputImgFilename = NULL;
  const char *outputTagsImgFilename = NULL;

  if (argc == 2) {
    inputImgFilename = argv[1];
    // Default to "outtags.png"
    outputTagsImgFilename = "outtags.png";
    
    // In the special case where the inputImgFilename is fully qualified, cd to the directory
    // indicated by the path. This is useful so that just a fully qualified image path can
    // be passed as the first argument without having to explicitly set the process working dir
    // since Xcode tends to get that detail wrong when invoking profiling tools.
    
    bool containsSlash = false;
    int lastSlashOffset = -1;
    
    for ( char *ptr = (char*)inputImgFilename; *ptr != '\0' ; ptr++ ) {
      if (*ptr == '/') {
        containsSlash = true;
        lastSlashOffset = int(ptr - (char*)inputImgFilename);
      }
    }
    
    if (containsSlash) {
      char *dirname = strdup((char*)inputImgFilename);
      assert(lastSlashOffset >= 0);
      dirname[lastSlashOffset] = '\0';
      
      inputImgFilename = inputImgFilename + lastSlashOffset + 1;
      
      cout << "cd \"" << dirname << "\"" << endl;
      chdir(dirname);
      
      free(dirname);
    }
  } else if (argc != 3) {
    cerr << "usage : " << argv[0] << " IMAGE ?TAGS_IMAGE?" << endl;
    exit(1);
  } else if (argc == 3) {
    inputImgFilename = argv[1];
    outputTagsImgFilename = argv[2];
  }

  cout << "read \"" << inputImgFilename << "\"" << endl;
  
  Mat inputImg = imread(inputImgFilename, CV_LOAD_IMAGE_COLOR);
  if( inputImg.empty() ) {
    cerr << "could not read \"" << inputImgFilename << "\" as image data" << endl;
    exit(1);
  }
  
  assert(inputImg.channels() == 3);
  
  if (inputImg.rows == 0 || inputImg.cols == 0) {
    cerr << "invalid size " << inputImg.size() << " for image data" << endl;
    exit(1);
  }
  
  Mat resultImg;
  
  bool worked = clusteringCombine(inputImg, resultImg);
  if (!worked) {
    cerr << "cluster combine operation failed " << endl;
    exit(1);
  }
  
  imwrite(outputTagsImgFilename, resultImg);
  
  cout << "wrote " << outputTagsImgFilename << endl;
  
  exit(0);
}

// Main method that implements the cluster combine logic

bool clusteringCombine(Mat &inputImg, Mat &resultImg)
{
  const bool debug = true;
  const bool debugWriteIntermediateFiles = true;
  
  // Alloc object on stack
  SuperpixelImage spImage;
  //
  // Ref to object allocated on heap
//  Ptr<SuperpixelImage> spImagePtr = new SuperpixelImage();
//  SuperpixelImage &spImage = *spImagePtr;
  
  // Constant for block of 4x4 based map
  
  const int superpixelDim = 4;
  int blockWidth = inputImg.cols / superpixelDim;
  if ((inputImg.cols % superpixelDim) != 0) {
    blockWidth++;
  }
  int blockHeight = inputImg.rows / superpixelDim;
  if ((inputImg.rows % superpixelDim) != 0) {
    blockHeight++;
  }
  
  assert((blockWidth * superpixelDim) >= inputImg.cols);
  assert((blockHeight * superpixelDim) >= inputImg.rows);
  
  // Run SRM logic to generate initial segmentation based on statistical "alikeness".
  // Very large regions are likely to be very alike or even contain many pixels that
  // are identical.
  
  bool worked;
  
  Mat srmTags;
  
  worked = srmMultiSegment(inputImg, srmTags);
  
  if (!worked) {
    return false;
  }
  
  // Generate a second segmentation of the same image but at a higher precision
  // setting so that areas that may have been segmented into the same region
  // in the less precise segmentation get split by this segmentation
    
  // Scan the tags generated by SRM and create superpixels of vario
  
  worked = SuperpixelImage::parse(srmTags, spImage);
  
  if (!worked) {
    return false;
  }
  
  // Dump image that shows the input superpixels written with a colortable
  
  resultImg = inputImg.clone();
  resultImg = (Scalar) 0;
  
  sranddev();
  
  if (debugWriteIntermediateFiles) {
    generateStaticColortable(inputImg, spImage);
  }
  
  if (debugWriteIntermediateFiles) {
    writeTagsWithStaticColortable(spImage, resultImg);
    imwrite("tags_init.png", resultImg);
  }
  
  cout << "started with " << spImage.superpixels.size() << " superpixels" << endl;
  
  // Scan superpixels to determine containment tree
  
  vector<int32_t> srmInsideOutOrder;
  
  {
    // Fill with UID+1
    
    spImage.fillMatrixWithSuperpixelTags(srmTags);
    
    // Scan SRM superpixel regions in terms of containment, this generates a tree
    // where each UID can contain 1 to N children.
    
    unordered_map<int32_t, vector<int32_t> > containsTreeMap;
    
    vector<int32_t> rootTags = recurseSuperpixelContainment(spImage, srmTags, containsTreeMap);
    
    for ( auto &pair : containsTreeMap ) {
      uint32_t tag = pair.first;
      vector<int32_t> children = pair.second;
      
      cout << "for srm superpixels tag " << tag << " num children are " << children.size() << endl;
      for ( int32_t childTag : children ) {
        cout << childTag << endl;
      }
    }
    
    stack<int32_t> insideOutStack;
    
    // Lambda
    auto lambdaFunc = [&](int32_t tag, const vector<int32_t> &children)->void {
      fprintf(stdout, "tag %9d has %5d children\n", tag, (int)children.size());
      
      insideOutStack.push(tag);
    };
    
    recurseSuperpixelIterate(rootTags, containsTreeMap, lambdaFunc);
    
    // Print in stack order

    fprintf(stdout, "inside out order\n");
    
    while (!insideOutStack.empty())
    {
      int32_t tag = insideOutStack.top();
      fprintf(stdout, "tag %5d has %5d children\n", tag, (int)containsTreeMap[tag].size());
      insideOutStack.pop();
      
      srmInsideOutOrder.push_back(tag);
    }
    
  }
  
  // Scan all superpixels and implement region merge and split based on the input pixels
  
  {
    
    Mat maskMat(inputImg.rows, inputImg.cols, CV_8UC1);
    Mat mergeMat(inputImg.rows, inputImg.cols, CV_8UC3);
    
    mergeMat = Scalar(0, 0, 0);
    
    // Calculate adler before rescanning and attempting region merges
    // in case the identical regions get regenerated and there is no
    // reason to reparse.
    
    uint32_t tagsAdlerBeforeMerge = 0;
    
    for ( int y = 0; y < srmTags.rows; y++ ) {
      for ( int x = 0; x < srmTags.cols; x++ ) {
        Vec3b vec = srmTags.at<Vec3b>(y, x);
        uint32_t pixel = Vec3BToUID(vec);
        tagsAdlerBeforeMerge = my_adler32(tagsAdlerBeforeMerge, (unsigned char const *)&pixel, sizeof(uint32_t), 0);
      }
    }
    
    // Quant the entire image into small 4x4 blocks and then generate histograms
    // for each block. The histogram data can be scanned significantly faster
    // that rereading all the original pixel info.
    
    unordered_map<Coord, HistogramForBlock> coordToBlockHistogramMap;
    
    Mat blockBasedQuantMat = genHistogramsForBlocks(inputImg, coordToBlockHistogramMap, blockWidth, blockHeight, superpixelDim);
    
    // Loop over superpixels starting at the most contained and working outwards
    
    for ( int32_t tag : srmInsideOutOrder ) {
      if (debug) {
        Superpixel *spPtr = spImage.getSuperpixelPtr(tag);
        cout << "process tag " << tag << " containing " << spPtr->coords.size() << endl;
      }
      
      // Copy current merge mask state into mask
      
      for ( int y = 0; y < mergeMat.rows; y++ ) {
        for ( int x = 0; x < mergeMat.cols; x++ ) {
          Vec3b vec = mergeMat.at<Vec3b>(y, x);
          uint32_t pixel = Vec3BToUID(vec);
          uint8_t bval;
          if (pixel == 0x0) {
            bval = 0;
          } else {
            bval = 0xFF;
          }
          maskMat.at<uint8_t>(y, x) = bval;
        }
      }
      
      bool maskWritten =
      captureRegionMask(spImage, inputImg, srmTags, tag, blockWidth, blockHeight, superpixelDim, maskMat, blockBasedQuantMat);
      
      if (maskWritten)
      {
        std::stringstream fnameStream;
        fnameStream << "srm" << "_tag_" << tag << "_region_mask" << ".png";
        string fname = fnameStream.str();
        
        imwrite(fname, maskMat);
        cout << "wrote " << fname << endl;
        cout << "";
      }
      
      if (maskWritten) {
        vector<Point> locations;
        findNonZero(maskMat, locations);
        
        for ( Point p : locations ) {
          int x = p.x;
          int y = p.y;
          
          Coord c(x, y);
          
          Vec3b vec;
          
          vec = mergeMat.at<Vec3b>(y, x);
          
          if (vec[0] == 0x0 && vec[1] == 0x0 && vec[2] == 0x0) {
            // This pixel has not been seen before, copy from srmTags to mergeMat
            
            vec = srmTags.at<Vec3b>(y, x);
            
#if defined(DEBUG)
            // Find spImage tag associated with this (x,y) coordinate
            uint32_t renderedTag = Vec3BToUID(vec);
            assert(renderedTag != 0);
            Superpixel *srcSpPtr = spImage.getSuperpixelPtr(renderedTag);
            if (srcSpPtr == NULL) {
              printf("coord (%5d, %5d) = 0x%08X aka %d\n", x, y, renderedTag, renderedTag);
            }
            assert(srcSpPtr);
#endif // DEBUG
            
            mergeMat.at<Vec3b>(y, x) = vec;
          } else {
            // Attempting to merge already merged (x,y) location
            uint32_t mergedTag = Vec3BToUID(vec);
            printf("coord (%5d, %5d) = 0x%08X aka %d\n", x, y, mergedTag, mergedTag);
            vec = srmTags.at<Vec3b>(y, x);
            uint32_t toBeMergedTag = Vec3BToUID(vec);
            
            const bool allowReplaceWithSameTag = false;
            
            if (allowReplaceWithSameTag) {
              if (mergedTag != toBeMergedTag) {
                printf("coord (%5d, %5d) = attempted merge 0x%08X aka %d\n", x, y, toBeMergedTag, toBeMergedTag);
                assert(0);
              }
            } else {
              printf("coord (%5d, %5d) = attempted merge 0x%08X aka %d\n", x, y, toBeMergedTag, toBeMergedTag);
              assert(0);
            }
          }
        } // foreach locations
        
        if (debugWriteIntermediateFiles) {
          std::stringstream fnameStream;
          fnameStream << "srm" << "_tag_" << tag << "_merge_region" << ".png";
          string fname = fnameStream.str();
          
          imwrite(fname, mergeMat);
          cout << "wrote " << fname << endl;
          cout << "" << endl;
        }
        
      } // if maskWritten
      
    } // foreach tag in sorted superpixels
    
    // Copy any pixel from srmTags unless mergeMat is set
    // to a non-zero value already.
    
    for ( int y = 0; y < srmTags.rows; y++ ) {
      for ( int x = 0; x < srmTags.cols; x++ ) {
        Vec3b vec = mergeMat.at<Vec3b>(y, x);
        
        if (vec[0] == 0x0 && vec[1] == 0x0 && vec[2] == 0x0) {
          vec = srmTags.at<Vec3b>(y, x);
          mergeMat.at<Vec3b>(y, x) = vec;
          
          // FIXME: collect any existing tags into vectors of Coord and then
          // append new UIDs for each such vector of coords to the merge mat
          // to avoid problem with UID being reused.
          
          if ((debug) && true) {
            uint32_t pixel = Vec3BToUID(vec);
            fprintf(stdout, "copy existing tag at (%5d, %5d) = 0X%08X\n", x, y, pixel);
          }
        }
      }
    }
    
    if (debugWriteIntermediateFiles) {
      std::stringstream fnameStream;
      fnameStream << "srm_merged_all_regions" << ".png";
      string fname = fnameStream.str();
      
      imwrite(fname, mergeMat);
      cout << "wrote " << fname << endl;
      cout << "" << endl;
    }
    
    uint32_t tagsAdlerAfterMerge = 0;
    
    for ( int y = 0; y < srmTags.rows; y++ ) {
      for ( int x = 0; x < srmTags.cols; x++ ) {
        Vec3b vec = srmTags.at<Vec3b>(y, x);
        uint32_t pixel = Vec3BToUID(vec);
        tagsAdlerAfterMerge = my_adler32(tagsAdlerAfterMerge, (unsigned char const *)&pixel, sizeof(uint32_t), 0);
      }
    }
    
    // Don't do expensive reparsing if merge resulted in the exact same thing

    if (tagsAdlerBeforeMerge == tagsAdlerAfterMerge) {
      if (debug) {
        cout << "merge operation did not change any tags" << endl;
      }
    } else {
      spImage = SuperpixelImage();
      
      worked = SuperpixelImage::parse(mergeMat, spImage);
      
      if (!worked) {
        return false;
      }
    }
    
    // FIXME: If there have been no changes, no reason to reparse ? (check an adler32)
    
    // mergeMat now contains tags after a split and merge operation
    
  }
  
  // Generate result image after region based merging
  
  if (debugWriteIntermediateFiles) {
    generateStaticColortable(inputImg, spImage);
    writeTagsWithStaticColortable(spImage, resultImg);
    imwrite("tags_after_region_merge.png", resultImg);
  }
  
  // Done
  
  cout << "ended with " << spImage.superpixels.size() << " superpixels" << endl;
  
  return true;
}

