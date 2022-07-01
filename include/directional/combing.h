// This file is part of Directional, a library for directional field processing.
// Copyright (C) 2018 Amir Vaxman <avaxman@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef DIRECTIONAL_COMBING_H
#define DIRECTIONAL_COMBING_H


#include <Eigen/Core>
#include <queue>
#include <vector>
#include <cmath>
#include <igl/igl_inline.h>
#include <directional/CartesianField.h>
#include <directional/tree.h>
#include <directional/principal_matching.h>

namespace directional
{
  // Reorders the vectors in a face (preserving CCW) so that the prescribed matching across most edges, except a small set (called a seam), is an identity, making it ready for cutting and parameterization.
  // Important: if the Raw field in not CCW ordered, the result is unpredictable.
  // Input:
  //  rawField:   a RAW_FIELD uncombed cartesian field object
  //  faceIs_Cut: #F x 3 optionally prescribing the halfedges (corresponding to mesh faces) that must be a seam.
  // Output:
  //  combedField: the combed field object, also RAW_FIELD
  
  IGL_INLINE void combing(const directional::CartesianField& rawField,
                          directional::CartesianField& combedField,
                          const Eigen::MatrixXi& _faceIsCut=Eigen::MatrixXi())
  {
    using namespace Eigen;
    combedField.init_field(*(rawField.mesh), RAW_FIELD, rawField.N);
    Eigen::MatrixXi faceIsCut(rawField.intField.rows(),3);
    if (_faceIsCut.rows()==0)
      faceIsCut.setZero();
    else
      faceIsCut=_faceIsCut;
    
    VectorXi spaceTurns(rawField.intField.rows());
    
    //flood-filling through the matching to comb field
    
    //dual tree to find combing routes
    VectorXi visitedSpaces=VectorXi::Constant(rawField.intField.rows(),1,0);
    std::queue<std::pair<int,int> > spaceMatchingQueue;
    spaceMatchingQueue.push(std::pair<int,int>(0,0));
    MatrixXd combedIntField(combedField.intField.rows(), combedField.intField.cols());
    do{
      std::pair<int,int> currSpaceMatching=spaceMatchingQueue.front();
      spaceMatchingQueue.pop();
      if (visitedSpaces(currSpaceMatching.first))
        continue;
      visitedSpaces(currSpaceMatching.first)=1;
      
      //combing field to start from the matching index
      combedIntField.block(currSpaceMatching.first, 0, 1, 2*(rawField.N-currSpaceMatching.second))=rawField.intField.block(currSpaceMatching.first, 2*currSpaceMatching.second, 1, 2*(rawField.N-currSpaceMatching.second));
      combedIntField.block(currSpaceMatching.first, 2*(rawField.N-currSpaceMatching.second), 1, 2*currSpaceMatching.second)=rawField.intField.block(currSpaceMatching.first, 0, 1, 2*currSpaceMatching.second);
      
      spaceTurns(currSpaceMatching.first)=currSpaceMatching.second;
      
      for (int i=0;i<3;i++){
        int nextMatching=(rawField.matching(rawField.oneRing(currSpaceMatching.first,i)));
        int nextFace=(rawField.adjSpaces(rawField.oneRing(currSpaceMatching.first,i),0)==currSpaceMatching.first ? rawField.adjSpaces(rawField.oneRing(currSpaceMatching.first,i),1) : rawField.adjSpaces(rawField.oneRing(currSpaceMatching.first,i),0));
        nextMatching*=(rawField.adjSpaces(rawField.oneRing(currSpaceMatching.first,i),0)==currSpaceMatching.first ? 1.0 : -1.0);
        nextMatching=(nextMatching+currSpaceMatching.second+10*rawField.N)%rawField.N;  //killing negatives
        if ((nextFace!=-1)&&(!visitedSpaces(nextFace))&&(!faceIsCut(currSpaceMatching.first,i)))
          spaceMatchingQueue.push(std::pair<int,int>(nextFace, nextMatching));
        
      }
      
    }while (!spaceMatchingQueue.empty());
    
    combedField.set_intrinsic_field(combedIntField);
    combedField.matching.resize(rawField.adjSpaces.rows());
    //giving combed matching
    for (int i=0;i<rawField.adjSpaces.rows();i++){
      if ((rawField.adjSpaces(i,0)==-1)||(rawField.adjSpaces(i,1)==-1))
        combedField.matching(i)=-1;
      else
        combedField.matching(i)=(spaceTurns(rawField.adjSpaces(i,0))-spaceTurns(rawField.adjSpaces(i,1))+rawField.matching(i)+1000*rawField.N)%rawField.N;
    }
  }
}




#endif


